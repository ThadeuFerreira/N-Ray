#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <gltf_scene.h>
#include <vulkan_compute_preview.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <sstream>
#include <vector>

#include "vulkan_triangle_spv.h"
#include "tut28_star_nest_comp_spv.h"
#include "tut28_lets_self_reflect_comp_spv.h"
#include "tut28_spiral_galaxy_comp_spv.h"
#include "tut28_battered_alien_planet_comp_spv.h"
#include "tut28_flux_core_comp_spv.h"
#include "vulkan_gltf_flat_comp_spv.h"

namespace {
struct PushConstants {
	int width = 0;
	int height = 0;
	float time = 0.0f;
	uint32_t sampleIndex = 0u;
	glm::vec4 cameraPos = glm::vec4(0.0f);
	glm::vec4 cameraForward = glm::vec4(0.0f);
	glm::vec4 cameraRight = glm::vec4(0.0f);
	glm::vec4 cameraUp = glm::vec4(0.0f);
	glm::vec4 cameraParams = glm::vec4(0.0f);
	glm::uvec4 sceneCounts = glm::uvec4(0u);
};

static_assert(sizeof(PushConstants) == 112, "Push constants must match Vulkan preview shaders.");
static_assert(nray_vulkan_triangle_comp_spv_len % 4 == 0, "SPIR-V bytecode size must be a multiple of 4.");
static_assert(nray_vulkan_gltf_flat_comp_spv_len % 4 == 0, "SPIR-V bytecode size must be a multiple of 4.");

struct GpuTriIntersect {
	glm::vec4 a;
	glm::vec4 eA;
	glm::vec4 eB;
	glm::uvec4 ids;
};

struct GpuTriShading {
	glm::vec4 aN;
	glm::vec4 bN;
	glm::vec4 cN;
	glm::uvec4 ids;
};

struct GpuMaterial {
	glm::vec4 baseColor;    // rgb albedo, a opacity
	glm::vec4 params;       // roughness, metalness, emissionIntensity, transmission
	glm::vec4 emissionIor;  // rgb emissionCol, a IOR
};

struct GpuBvhNode {
	glm::vec4 minBounds;
	glm::vec4 maxBounds;
	glm::uvec4 meta;
};

// Per-dispatch render/sky settings, uploaded to the binding 6 SSBO. Mirrors the
// `cfg` block in vulkan_gltf_flat.comp; keep the field packing in lockstep.
struct GpuSettings {
	glm::vec4 renderParams; // x=maxBounces, y=rrMinBounces, z=russianRoulette(0/1), w=exposure
	glm::vec4 skyParams;    // x=skyIntensity, y=enableSky(0/1), z=enableSun(0/1), w=contrast
	glm::vec4 sunDir;       // xyz direction, w=sunAngle (degrees)
	glm::vec4 sunColor;     // xyz color, w=sunIntensity
};

static_assert(sizeof(GpuTriIntersect) == 64, "GpuTriIntersect must match std430 shader layout.");
static_assert(sizeof(GpuTriShading) == 64, "GpuTriShading must match std430 shader layout.");
static_assert(sizeof(GpuMaterial) == 48, "GpuMaterial must match std430 shader layout.");
static_assert(sizeof(GpuBvhNode) == 48, "GpuBvhNode must match std430 shader layout.");
static_assert(sizeof(GpuSettings) == 64, "GpuSettings must match std430 shader layout.");

const char* vkResultName(VkResult result) {
	switch (result) {
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	default: return "VK_ERROR_UNKNOWN";
	}
}

std::string vkErrorMessage(const char* op, VkResult result) {
	std::ostringstream out;
	out << op << " failed: " << vkResultName(result) << " (" << static_cast<int>(result) << ")";
	return out.str();
}

uint32_t ceilDiv(uint32_t value, uint32_t divisor) {
	return (value + divisor - 1) / divisor;
}

float computeModelRayBias(const GltfPreviewScene& scene) {
	glm::vec3 extent = scene.boundsMax - scene.boundsMin;
	float diagonal = glm::length(extent);
	if (!std::isfinite(diagonal) || diagonal <= 0.0f) {
		return 0.0001f;
	}

	return std::clamp(diagonal * 0.00001f, 0.000001f, 0.01f);
}

struct ShaderEntry {
	const char* name;
	const unsigned char* spv;
	unsigned int len;
	bool requiresModel;
};

struct ModelEntry {
	const char* name;
	const char* path;
};

static const ShaderEntry kShaders[] = {
	{ "Hello World Triangle",       nray_vulkan_triangle_comp_spv,          nray_vulkan_triangle_comp_spv_len,          false },
	{ "Star Nest",                  tut28_star_nest_comp_spv,               tut28_star_nest_comp_spv_len,               false },
	{ "Lets Self Reflect",          tut28_lets_self_reflect_comp_spv,       tut28_lets_self_reflect_comp_spv_len,       false },
	{ "Spiral Galaxy",              tut28_spiral_galaxy_comp_spv,           tut28_spiral_galaxy_comp_spv_len,           false },
	{ "Battered Alien Planet",      tut28_battered_alien_planet_comp_spv,   tut28_battered_alien_planet_comp_spv_len,   false },
	{ "Flux Core",                  tut28_flux_core_comp_spv,               tut28_flux_core_comp_spv_len,               false },
	{ "Nissan S15 glTF Model",      nray_vulkan_gltf_flat_comp_spv,         nray_vulkan_gltf_flat_comp_spv_len,         true  },
};
static const int kShaderCount = static_cast<int>(sizeof(kShaders) / sizeof(kShaders[0]));

// Total descriptor set bindings: 0=pixels, 1-4=scene SSBOs, 5=accum, 6=settings.
static constexpr uint32_t kTotalBindings = 7;
// Scene SSBO bindings start at index 1 (binding 0 is the pixel buffer).
static constexpr uint32_t kFirstSceneBinding = 1;
// Named binding slots — update if the layout in vulkan_gltf_flat.comp changes.
static constexpr uint32_t kBindingPixels   = 0;
static constexpr uint32_t kBindingAccum    = 5;
static constexpr uint32_t kBindingSettings = 6;

static const ModelEntry kModels[] = {
	{ "Nissan S15 Silvia", "assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf" },
	{ "LB Silhouette Murcielago GT Evo", "assets/2024_lbsilhouette_works_murcielago_gt_evo/scene.gltf" },
	{ "Torvosaurus Tanneri", "assets/accurate_torvosaurus_tanneri/scene.gltf" },
	{ "Beretta ARX160", "assets/beretta_arx160/scene.gltf" },
	{ "Beretta M9", "assets/beretta_m9_gameready/scene.gltf" },
	{ "Hulk Infinity Hulk", "assets/hulk_infinity_hulk/scene.gltf" },
	{ "Luna Snow", "assets/luna_snow_-_sonic_trailblazer/scene.gltf" },
	{ "Wolverine X-2099", "assets/wolverine_-_wolverine_-_x-2099_bundle/scene.gltf" },
};
static const int kModelCount = static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

}

struct VulkanComputePreview::Impl {
	struct StorageBuffer {
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceSize allocationSize = 0;
		size_t size = 0;
		bool memoryCoherent = false;
	};

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queueFamily = 0;

	VkBuffer pixelBuffer = VK_NULL_HANDLE;
	VkDeviceMemory pixelMemory = VK_NULL_HANDLE;
	void* mappedPixels = nullptr;
	VkDeviceSize pixelAllocationSize = 0;
	size_t pixelBufferSize = 0;
	bool pixelMemoryCoherent = false;

	// HDR accumulation buffer (binding 5): one glm::vec4 per pixel in device-local
	// storage. The shader sums radiance here across dispatches; command buffers
	// clear it with vkCmdFillBuffer on reset. Never read back to the host.
	VkBuffer accumBuffer = VK_NULL_HANDLE;
	VkDeviceMemory accumMemory = VK_NULL_HANDLE;
	VkDeviceSize accumAllocationSize = 0;
	size_t accumBufferSize = 0;

	// Per-dispatch render/sky settings (binding 6): a single GpuSettings record,
	// persistently mapped host-visible, rewritten each render().
	VkBuffer settingsBuffer = VK_NULL_HANDLE;
	VkDeviceMemory settingsMemory = VK_NULL_HANDLE;
	void* mappedSettings = nullptr;
	VkDeviceSize settingsAllocationSize = 0;
	size_t settingsBufferSize = 0;
	bool settingsMemoryCoherent = false;

	// Progressive accumulation lifecycle. currentSample counts completed samples;
	// the next dispatch uses it as the push-constant sample index. accumNeedsClear
	// records a GPU clear before the next dispatch.
	uint32_t currentSample = 0;
	uint32_t maxSamplesCached = 1;
	bool accumNeedsClear = true;

	// Scene storage buffers are bound to descriptor set bindings 1..4 (binding 0
	// is the pixel buffer). The enum order is the (binding - 1) index, and must
	// stay aligned with the shader's binding declarations and the sceneCounts
	// packing in render().
	enum SceneBuffer {
		SCENE_TRI_ISECT = 0,
		SCENE_TRI_SHADING,
		SCENE_MATERIAL,
		SCENE_BVH,
		SCENE_BUFFER_COUNT
	};

	StorageBuffer dummyBuffer;
	std::array<StorageBuffer, SCENE_BUFFER_COUNT> sceneBuffers;
	GltfPreviewScene modelScene;
	bool modelBuffersReady = false;
	// triCount, bvhNodeCount, materialCount, modelReady — cached once at load so
	// render() doesn't recompute the (constant) sizes every frame.
	glm::uvec4 sceneCounts = glm::uvec4(0u);

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkShaderModule shaderModule = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties cachedMemoryProps{};

	VkQueryPool timestampPool = VK_NULL_HANDLE;
	float timestampPeriod = 0.0f;
	bool timestampSupported = false;
	bool memBudgetSupported = false;
	uint64_t localHeapBytes = 0;
	uint32_t localHeapIndex = UINT32_MAX;
	uint64_t localHeapUsed = 0;
	double lastGpuMs = 0.0;
	uint32_t frameCount = 0;

	int selectedShader = 0;
	int selectedModel = 0;
	int renderWidth = 0;
	int renderHeight = 0;
	bool initialized = false;
	bool volkReady = false;
	bool instanceCleanupUnsafe = false;
	std::string status = "Vulkan preview not initialized";
	std::string deviceName = "unknown device";

	bool initialize(int width, int height) {
		destroyResources();

		if (width <= 0 || height <= 0) {
			status = "Vulkan preview disabled: invalid render size";
			return false;
		}

		renderWidth = width;
		renderHeight = height;
		instanceCleanupUnsafe = false;

		if (!volkReady) {
			VkResult result = volkInitialize();
			if (result != VK_SUCCESS) {
				status = vkErrorMessage("volkInitialize", result);
				return false;
			}
			volkReady = true;
		}

		if (!createInstance() ||
			!selectPhysicalDevice() ||
			!createDevice() ||
			!createPixelBuffer() ||
			!createDummySceneBuffer() ||
			!loadModelSceneResources() ||
			!createAccumBuffer() ||
			!createSettingsBuffer() ||
			!createDescriptorSetLayout() ||
			!createDescriptorPoolAndSet() ||
			!createPipeline() ||
			!createCommandResources()) {
			if (instanceCleanupUnsafe) {
				abandonUnsafePartialInstance();
			}
			else {
				destroyResources();
			}
			return false;
		}

		initialized = true;
		setActiveStatus();
		return true;
	}

	bool resize(int width, int height) {
		if (initialized && width == renderWidth && height == renderHeight) {
			return true;
		}
		if (!initialized) {
			return initialize(width, height);
		}
		return resizePixelResources(width, height);
	}

	bool resizePixelResources(int width, int height) {
		if (width <= 0 || height <= 0) {
			return fail("Vulkan preview disabled: invalid render size");
		}

		VkResult result = vkDeviceWaitIdle(device);
		if (result != VK_SUCCESS) {
			return failVk("vkDeviceWaitIdle", result);
		}

		destroyDescriptorPool();
		destroyPixelBuffer();
		destroyAccumBuffer();

		renderWidth = width;
		renderHeight = height;
		if (!createPixelBuffer() || !createAccumBuffer() || !createDescriptorPoolAndSet()) {
			destroyDescriptorPool();
			destroyPixelBuffer();
			destroyAccumBuffer();
			initialized = false;
			return false;
		}

		setActiveStatus();
		return true;
	}

	bool render(float timeSeconds, const VulkanPreviewCamera& camera, const VulkanPreviewSettings& settings, std::vector<RenderPixel>& pixels) {
		if (!initialized) {
			return false;
		}

		const ShaderEntry& selectedEntry = kShaders[selectedShader >= 0 && selectedShader < kShaderCount ? selectedShader : 0];
		maxSamplesCached = static_cast<uint32_t>(std::max(1, settings.maxSamples));
		if (settings.resetAccumulation) {
			resetAccumState();
		}

		// Upload the per-dispatch render/sky settings (binding 6).
		GpuSettings gpuSettings{};
		gpuSettings.renderParams = glm::vec4(
			static_cast<float>(settings.maxBounces),
			static_cast<float>(settings.rrMinBounces),
			settings.russianRoulette ? 1.0f : 0.0f,
			settings.exposure
		);
		gpuSettings.skyParams = glm::vec4(
			settings.skyIntensity,
			settings.enableSky ? 1.0f : 0.0f,
			settings.enableSun ? 1.0f : 0.0f,
			settings.contrast
		);
		gpuSettings.sunDir = glm::vec4(settings.sunDir, settings.sunAngle);
		gpuSettings.sunColor = glm::vec4(settings.sunColor, settings.sunIntensity);
		std::memcpy(mappedSettings, &gpuSettings, sizeof(GpuSettings));
		if (!flushMappedRange(settingsMemory, settingsMemoryCoherent, "vkFlushMappedMemoryRanges settings")) {
			return false;
		}

		VkResult result = vkResetFences(device, 1, &fence);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkResetFences", result);
			return false;
		}

		result = vkResetCommandBuffer(commandBuffer, 0);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkResetCommandBuffer", result);
			return false;
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkBeginCommandBuffer", result);
			return false;
		}

		if (accumNeedsClear) {
			vkCmdFillBuffer(commandBuffer, accumBuffer, 0, VK_WHOLE_SIZE, 0);

			VkBufferMemoryBarrier clearBarrier{};
			clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			clearBarrier.buffer = accumBuffer;
			clearBarrier.offset = 0;
			clearBarrier.size = VK_WHOLE_SIZE;
			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0,
				nullptr,
				1,
				&clearBarrier,
				0,
				nullptr
			);

			accumNeedsClear = false;
		}
		else {
			VkBufferMemoryBarrier sampleBarrier{};
			sampleBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			sampleBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			sampleBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			sampleBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sampleBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sampleBarrier.buffer = accumBuffer;
			sampleBarrier.offset = 0;
			sampleBarrier.size = VK_WHOLE_SIZE;
			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0,
				nullptr,
				1,
				&sampleBarrier,
				0,
				nullptr
			);
		}

		float modelRayBias = selectedEntry.requiresModel && modelBuffersReady
			? computeModelRayBias(modelScene)
			: 0.0001f;

		PushConstants pushConstants{
			renderWidth,
			renderHeight,
			timeSeconds,
			currentSample,
			glm::vec4(camera.position, 0.0f),
			glm::vec4(camera.forward, 0.0f),
			glm::vec4(camera.right, 0.0f),
			glm::vec4(camera.up, 0.0f),
			glm::vec4(camera.verticalScale, camera.aspect, modelRayBias, 0.0f),
			sceneCounts
		};

		if (timestampSupported && timestampPool != VK_NULL_HANDLE) {
			vkCmdResetQueryPool(commandBuffer, timestampPool, 0, 2);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool, 0);
		}

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pushConstants);
		vkCmdDispatch(commandBuffer, ceilDiv(static_cast<uint32_t>(renderWidth), 16), ceilDiv(static_cast<uint32_t>(renderHeight), 16), 1);

		if (timestampSupported && timestampPool != VK_NULL_HANDLE) {
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool, 1);
		}

		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			1,
			&memoryBarrier,
			0,
			nullptr,
			0,
			nullptr
		);

		result = vkEndCommandBuffer(commandBuffer);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkEndCommandBuffer", result);
			return false;
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		result = vkQueueSubmit(queue, 1, &submitInfo, fence);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkQueueSubmit", result);
			return false;
		}

		result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("vkWaitForFences", result);
			return false;
		}

		if (!pixelMemoryCoherent) {
			VkMappedMemoryRange range{};
			range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			range.memory = pixelMemory;
			range.offset = 0;
			range.size = VK_WHOLE_SIZE;
			result = vkInvalidateMappedMemoryRanges(device, 1, &range);
			if (result != VK_SUCCESS) {
				status = vkErrorMessage("vkInvalidateMappedMemoryRanges", result);
				return false;
			}
		}

		if (timestampSupported && timestampPool != VK_NULL_HANDLE) {
			uint64_t timestamps[2] = {};
			VkResult tsResult = vkGetQueryPoolResults(
				device, timestampPool, 0, 2,
				sizeof(timestamps), timestamps, sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
			);
			if (tsResult == VK_SUCCESS) {
				uint64_t delta = timestamps[1] - timestamps[0];
				lastGpuMs = static_cast<double>(delta) * static_cast<double>(timestampPeriod) / 1e6;
			}
		}

		if (memBudgetSupported && localHeapIndex != UINT32_MAX) {
			VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
			budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
			VkPhysicalDeviceMemoryProperties2 memProps2{};
			memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			memProps2.pNext = &budgetProps;
			vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProps2);
			localHeapUsed = budgetProps.heapUsage[localHeapIndex];
		}

		frameCount++;
		currentSample++;
		pixels.resize(static_cast<size_t>(renderWidth) * static_cast<size_t>(renderHeight));
		std::memcpy(pixels.data(), mappedPixels, pixelBufferSize);
		return true;
	}

	void shutdown() {
		destroyResources();
		status = "Vulkan preview shut down";
	}

	void destroyResources() {
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}

		if (timestampPool != VK_NULL_HANDLE) {
			vkDestroyQueryPool(device, timestampPool, nullptr);
			timestampPool = VK_NULL_HANDLE;
		}
		if (fence != VK_NULL_HANDLE) {
			vkDestroyFence(device, fence, nullptr);
			fence = VK_NULL_HANDLE;
		}
		if (commandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device, commandPool, nullptr);
			commandPool = VK_NULL_HANDLE;
			commandBuffer = VK_NULL_HANDLE;
		}
		if (pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
		if (pipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			pipelineLayout = VK_NULL_HANDLE;
		}
		if (shaderModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, shaderModule, nullptr);
			shaderModule = VK_NULL_HANDLE;
		}
		destroyDescriptorPool();
		if (descriptorSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			descriptorSetLayout = VK_NULL_HANDLE;
		}
		destroyStorageBuffer(dummyBuffer);
		for (StorageBuffer& buffer : sceneBuffers) {
			destroyStorageBuffer(buffer);
		}
		destroyPixelBuffer();
		destroyAccumBuffer();
		destroySettingsBuffer();
		if (device != VK_NULL_HANDLE) {
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
			queue = VK_NULL_HANDLE;
		}
		if (instance != VK_NULL_HANDLE) {
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		// Keep the Vulkan loader open for process lifetime. Some loader/driver
		// stacks can still own background state after a failed enumeration, and
		// dlclose via volkFinalize() has proven unsafe on that failure path.

		physicalDevice = VK_NULL_HANDLE;
		renderWidth = 0;
		renderHeight = 0;
		pixelAllocationSize = 0;
		pixelBufferSize = 0;
		pixelMemoryCoherent = false;
		resetAccumState();
		maxSamplesCached = 1;
		modelBuffersReady = false;
		modelScene = {};
		sceneCounts = glm::uvec4(0u);
		initialized = false;
		lastGpuMs = 0.0;
		localHeapUsed = 0;
		frameCount = 0;
		timestampSupported = false;
		memBudgetSupported = false;
	}

	void abandonUnsafePartialInstance() {
		instance = VK_NULL_HANDLE;
		physicalDevice = VK_NULL_HANDLE;
		renderWidth = 0;
		renderHeight = 0;
		initialized = false;
		instanceCleanupUnsafe = false;
	}

	void destroyDescriptorPool() {
		if (descriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
			descriptorSet = VK_NULL_HANDLE;
		}
	}

	void destroyPixelBuffer() {
		if (mappedPixels != nullptr) {
			vkUnmapMemory(device, pixelMemory);
			mappedPixels = nullptr;
		}
		if (pixelBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, pixelBuffer, nullptr);
			pixelBuffer = VK_NULL_HANDLE;
		}
		if (pixelMemory != VK_NULL_HANDLE) {
			vkFreeMemory(device, pixelMemory, nullptr);
			pixelMemory = VK_NULL_HANDLE;
		}
		pixelAllocationSize = 0;
		pixelBufferSize = 0;
		pixelMemoryCoherent = false;
	}

		// Create a persistently-mapped host-visible STORAGE_BUFFER for data the host
		// updates every dispatch, unlike the create-once createStorageBuffer helper
		// which unmaps after upload.
	bool createMappedBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
		VkDeviceSize& allocationSize, size_t byteSize, bool& coherent, const char* tag) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(byteSize);
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkCreateBuffer ") + tag).c_str(), result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetBufferMemoryRequirements(device, buffer, &memoryReqs);

		uint32_t memoryTypeIndex = 0;
		coherent = true;
		if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex)) {
			coherent = false;
			if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memoryTypeIndex)) {
				return fail(std::string("No host-visible memory type for Vulkan ") + tag + " buffer");
			}
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryReqs.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;

		result = vkAllocateMemory(device, &allocateInfo, nullptr, &memory);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkAllocateMemory ") + tag).c_str(), result);
		}
		allocationSize = memoryReqs.size;

		result = vkBindBufferMemory(device, buffer, memory, 0);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkBindBufferMemory ") + tag).c_str(), result);
		}

		result = vkMapMemory(device, memory, 0, allocationSize, 0, &mapped);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkMapMemory ") + tag).c_str(), result);
		}
		return true;
	}

	void destroyMappedBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped, VkDeviceSize& allocationSize) {
		if (mapped != nullptr) {
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
		if (buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
		}
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, memory, nullptr);
			memory = VK_NULL_HANDLE;
		}
		allocationSize = 0;
	}

	void resetAccumState() {
		currentSample = 0;
		accumNeedsClear = true;
	}

	// Flush a host write to non-coherent mapped memory so the device sees it.
	bool flushMappedRange(VkDeviceMemory memory, bool coherent, const char* op) {
		if (coherent) {
			return true;
		}
		VkMappedMemoryRange range{};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		VkResult result = vkFlushMappedMemoryRanges(device, 1, &range);
		if (result != VK_SUCCESS) {
			return failVk(op, result);
		}
		return true;
	}

	bool createAccumBuffer() {
		accumBufferSize = static_cast<size_t>(renderWidth) * static_cast<size_t>(renderHeight) * sizeof(glm::vec4);

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(accumBufferSize);
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &accumBuffer);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateBuffer accum", result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetBufferMemoryRequirements(device, accumBuffer, &memoryReqs);

		uint32_t memoryTypeIndex = 0;
		if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex)) {
			destroyAccumBuffer();
			return fail("No device-local memory type for Vulkan accumulation buffer");
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryReqs.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;

		result = vkAllocateMemory(device, &allocateInfo, nullptr, &accumMemory);
		if (result != VK_SUCCESS) {
			destroyAccumBuffer();
			return failVk("vkAllocateMemory accum", result);
		}
		accumAllocationSize = memoryReqs.size;

		result = vkBindBufferMemory(device, accumBuffer, accumMemory, 0);
		if (result != VK_SUCCESS) {
			destroyAccumBuffer();
			return failVk("vkBindBufferMemory accum", result);
		}

		resetAccumState();
		return true;
	}

	void destroyAccumBuffer() {
		if (accumBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, accumBuffer, nullptr);
			accumBuffer = VK_NULL_HANDLE;
		}
		if (accumMemory != VK_NULL_HANDLE) {
			vkFreeMemory(device, accumMemory, nullptr);
			accumMemory = VK_NULL_HANDLE;
		}
		accumAllocationSize = 0;
		accumBufferSize = 0;
	}

	bool createSettingsBuffer() {
		settingsBufferSize = sizeof(GpuSettings);
		if (!createMappedBuffer(settingsBuffer, settingsMemory, mappedSettings, settingsAllocationSize, settingsBufferSize, settingsMemoryCoherent, "settings")) {
			return false;
		}
		std::memset(mappedSettings, 0, settingsBufferSize);
		return flushMappedRange(settingsMemory, settingsMemoryCoherent, "vkFlushMappedMemoryRanges settings");
	}

	void destroySettingsBuffer() {
		destroyMappedBuffer(settingsBuffer, settingsMemory, mappedSettings, settingsAllocationSize);
		settingsBufferSize = 0;
		settingsMemoryCoherent = false;
	}

	bool fail(const std::string& message) {
		status = message;
		return false;
	}

	bool failVk(const char* op, VkResult result) {
		return fail(vkErrorMessage(op, result));
	}

	bool createInstance() {
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "N-Ray Vulkan Compute Preview";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.pEngineName = "N-Ray";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.apiVersion = VK_API_VERSION_1_1;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateInstance", result);
		}

		volkLoadInstance(instance);
		return true;
	}

	bool selectPhysicalDevice() {
		uint32_t deviceCount = 0;
		VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (result != VK_SUCCESS) {
			instanceCleanupUnsafe = true;
			return failVk("vkEnumeratePhysicalDevices count", result);
		}
		if (deviceCount == 0) {
			return fail("No Vulkan physical devices found");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		if (result != VK_SUCCESS) {
			instanceCleanupUnsafe = true;
			return failVk("vkEnumeratePhysicalDevices", result);
		}

		int bestScore = -1;
		for (VkPhysicalDevice candidate : devices) {
			uint32_t candidateQueueFamily = 0;
			if (!findComputeQueueFamily(candidate, candidateQueueFamily)) {
				continue;
			}

			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(candidate, &props);
			int score = physicalDeviceScore(props);
			if (score > bestScore) {
				bestScore = score;
				physicalDevice = candidate;
				queueFamily = candidateQueueFamily;
				deviceName = props.deviceName;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE) {
			return fail("No Vulkan compute queue family found");
		}

		VkPhysicalDeviceProperties selectedProps{};
		vkGetPhysicalDeviceProperties(physicalDevice, &selectedProps);
		timestampPeriod = selectedProps.limits.timestampPeriod;

		{
			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
			if (queueFamily < count) {
				timestampSupported = families[queueFamily].timestampValidBits > 0 && timestampPeriod > 0.0f;
			}
		}

		{
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &cachedMemoryProps);
			localHeapIndex = UINT32_MAX;
			localHeapBytes = 0;
			for (uint32_t i = 0; i < cachedMemoryProps.memoryHeapCount; i++) {
				if ((cachedMemoryProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
					if (localHeapIndex == UINT32_MAX || cachedMemoryProps.memoryHeaps[i].size > localHeapBytes) {
						localHeapIndex = i;
						localHeapBytes = cachedMemoryProps.memoryHeaps[i].size;
					}
				}
			}
		}

		return true;
	}

	bool findComputeQueueFamily(VkPhysicalDevice candidate, uint32_t& familyIndex) const {
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

		for (uint32_t i = 0; i < queueFamilyCount; i++) {
			if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
				familyIndex = i;
				return true;
			}
		}

		return false;
	}

	int physicalDeviceScore(const VkPhysicalDeviceProperties& props) const {
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			return 3;
		}
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			return 2;
		}
		return 1;
	}

	bool createDevice() {
		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		memBudgetSupported = false;
		{
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> exts(extCount);
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, exts.data());
			for (const auto& ext : exts) {
				if (strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
					memBudgetSupported = true;
					break;
				}
			}
		}

		const char* memBudgetExt = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		if (memBudgetSupported) {
			createInfo.enabledExtensionCount = 1;
			createInfo.ppEnabledExtensionNames = &memBudgetExt;
		}

		VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateDevice", result);
		}

		volkLoadDevice(device);
		vkGetDeviceQueue(device, queueFamily, 0, &queue);
		return true;
	}

	bool findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags preferredFlags, uint32_t& memoryTypeIndex) const {
		for (uint32_t i = 0; i < cachedMemoryProps.memoryTypeCount; i++) {
			bool typeSupported = (typeBits & (1u << i)) != 0;
			bool flagsSupported = (cachedMemoryProps.memoryTypes[i].propertyFlags & preferredFlags) == preferredFlags;
			if (typeSupported && flagsSupported) {
				memoryTypeIndex = i;
				return true;
			}
		}

		return false;
	}

	void destroyStorageBuffer(StorageBuffer& resource) {
		if (resource.buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, resource.buffer, nullptr);
			resource.buffer = VK_NULL_HANDLE;
		}
		if (resource.memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, resource.memory, nullptr);
			resource.memory = VK_NULL_HANDLE;
		}
		resource.allocationSize = 0;
		resource.size = 0;
		resource.memoryCoherent = false;
	}

	bool createStorageBuffer(StorageBuffer& resource, const void* data, size_t byteSize) {
		destroyStorageBuffer(resource);

		if (byteSize == 0) {
			byteSize = sizeof(uint32_t) * 4;
			data = nullptr;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(byteSize);
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &resource.buffer);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateBuffer scene", result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetBufferMemoryRequirements(device, resource.buffer, &memoryReqs);

		uint32_t memoryTypeIndex = 0;
		resource.memoryCoherent = true;
		if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex)) {
			resource.memoryCoherent = false;
			if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memoryTypeIndex)) {
				destroyStorageBuffer(resource);
				return fail("No host-visible memory type for Vulkan scene buffer");
			}
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryReqs.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;

		result = vkAllocateMemory(device, &allocateInfo, nullptr, &resource.memory);
		if (result != VK_SUCCESS) {
			destroyStorageBuffer(resource);
			return failVk("vkAllocateMemory scene", result);
		}

		resource.allocationSize = memoryReqs.size;
		resource.size = byteSize;

		result = vkBindBufferMemory(device, resource.buffer, resource.memory, 0);
		if (result != VK_SUCCESS) {
			destroyStorageBuffer(resource);
			return failVk("vkBindBufferMemory scene", result);
		}

		void* mapped = nullptr;
		result = vkMapMemory(device, resource.memory, 0, resource.allocationSize, 0, &mapped);
		if (result != VK_SUCCESS) {
			destroyStorageBuffer(resource);
			return failVk("vkMapMemory scene", result);
		}

		if (data != nullptr) {
			std::memcpy(mapped, data, byteSize);
		}
		else {
			std::memset(mapped, 0, byteSize);
		}

		bool flushed = flushMappedRange(resource.memory, resource.memoryCoherent, "vkFlushMappedMemoryRanges scene");
		vkUnmapMemory(device, resource.memory);
		if (!flushed) {
			destroyStorageBuffer(resource);
			return false;
		}
		return true;
	}

	bool createDummySceneBuffer() {
		uint32_t zero[4] = {};
		return createStorageBuffer(dummyBuffer, zero, sizeof(zero));
	}

	bool loadModelSceneResources() {
		modelBuffersReady = false;
		sceneCounts = glm::uvec4(0u);
		modelScene = {};
		for (StorageBuffer& buffer : sceneBuffers) {
			destroyStorageBuffer(buffer);
		}

		int modelIndex = selectedModel >= 0 && selectedModel < kModelCount ? selectedModel : 0;
		if (!loadGltfPreviewScene(kModels[modelIndex].path, modelScene)) {
			if (!modelScene.status.empty()) {
				modelScene.status = std::string(kModels[modelIndex].name) + ": " + modelScene.status;
			}
			return true;
		}
		modelScene.status = std::string(kModels[modelIndex].name) + ": " + modelScene.status;

		std::vector<GpuTriIntersect> gpuTriIsect;
		gpuTriIsect.reserve(modelScene.triIsect.size());
		for (const TriIntersect& tri : modelScene.triIsect) {
			// The compute glTF preview favors validation visibility over strict
			// material backface culling; future raster/PBR paths can honor glTF
			// doubleSided exactly.
			gpuTriIsect.push_back({
				glm::vec4(tri.a, 0.0f),
				glm::vec4(tri.eA, 0.0f),
				glm::vec4(tri.eB, 0.0f),
				glm::uvec4(tri.idx, 1u, 0u, 0u)
			});
		}

		std::vector<GpuTriShading> gpuTriShading;
		gpuTriShading.reserve(modelScene.tris.size());
		for (const Tri& tri : modelScene.tris) {
			gpuTriShading.push_back({
				glm::vec4(tri.aN, 0.0f),
				glm::vec4(tri.bN, 0.0f),
				glm::vec4(tri.cN, 0.0f),
				glm::uvec4(tri.materialIdx, tri.modelIdx, 0u, 0u)
			});
		}

		std::vector<GpuMaterial> gpuMaterials;
		gpuMaterials.reserve(modelScene.materials.size());
		for (size_t i = 0; i < modelScene.materials.size(); ++i) {
			const PBRMaterial& material = modelScene.materials[i];
			float opacity = i < modelScene.materialOpacity.size() ? modelScene.materialOpacity[i] : 1.0f;
			float transmission = i < modelScene.materialTransmission.size() ? modelScene.materialTransmission[i] : 0.0f;
			gpuMaterials.push_back({
				glm::vec4(material.albedo, opacity),
				glm::vec4(material.roughness, material.metalness, material.emissionIntensity, transmission),
				glm::vec4(material.emissionCol, material.IOR)
			});
		}

		std::vector<GpuBvhNode> gpuBvh;
		gpuBvh.reserve(modelScene.flatBvh.size());
		for (const CompactBVH& node : modelScene.flatBvh) {
			uint32_t startOrSecond = node.triCount > 0 ? node.startIndex : node.secondChild;
			gpuBvh.push_back({
				glm::vec4(node.min, 0.0f),
				glm::vec4(node.max, 0.0f),
				glm::uvec4(startOrSecond, node.triCount, node.axis, 0u)
			});
		}

		if (!createStorageBuffer(sceneBuffers[SCENE_TRI_ISECT], gpuTriIsect.data(), gpuTriIsect.size() * sizeof(GpuTriIntersect)) ||
			!createStorageBuffer(sceneBuffers[SCENE_TRI_SHADING], gpuTriShading.data(), gpuTriShading.size() * sizeof(GpuTriShading)) ||
			!createStorageBuffer(sceneBuffers[SCENE_MATERIAL], gpuMaterials.data(), gpuMaterials.size() * sizeof(GpuMaterial)) ||
			!createStorageBuffer(sceneBuffers[SCENE_BVH], gpuBvh.data(), gpuBvh.size() * sizeof(GpuBvhNode))) {
			modelScene.status = "glTF model Vulkan upload failed: " + status;
			for (StorageBuffer& buffer : sceneBuffers) {
				destroyStorageBuffer(buffer);
			}
			return true;
		}

		sceneCounts = glm::uvec4(
			static_cast<uint32_t>(modelScene.triIsect.size()),
			static_cast<uint32_t>(modelScene.flatBvh.size()),
			static_cast<uint32_t>(modelScene.materials.size()),
			1u
		);
		modelBuffersReady = true;
		return true;
	}

	const StorageBuffer& descriptorBufferForBinding(uint32_t binding) const {
		if (!modelBuffersReady || binding < kFirstSceneBinding || binding >= kFirstSceneBinding + SCENE_BUFFER_COUNT) {
			return dummyBuffer;
		}
		return sceneBuffers[binding - kFirstSceneBinding];
	}

	void setActiveStatus() {
		status = "Vulkan compute preview active on " + deviceName;
		if (!modelScene.status.empty()) {
			status += "\n";
			status += modelScene.status;
		}
	}

	bool createPixelBuffer() {
		pixelBufferSize = static_cast<size_t>(renderWidth) * static_cast<size_t>(renderHeight) * sizeof(RenderPixel);

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = static_cast<VkDeviceSize>(pixelBufferSize);
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &pixelBuffer);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateBuffer", result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetBufferMemoryRequirements(device, pixelBuffer, &memoryReqs);

		uint32_t memoryTypeIndex = 0;
		pixelMemoryCoherent = true;
		if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex)) {
			pixelMemoryCoherent = false;
			if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memoryTypeIndex)) {
				return fail("No host-visible memory type for Vulkan preview buffer");
			}
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryReqs.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;

		result = vkAllocateMemory(device, &allocateInfo, nullptr, &pixelMemory);
		if (result != VK_SUCCESS) {
			return failVk("vkAllocateMemory", result);
		}
		pixelAllocationSize = memoryReqs.size;

		result = vkBindBufferMemory(device, pixelBuffer, pixelMemory, 0);
		if (result != VK_SUCCESS) {
			return failVk("vkBindBufferMemory", result);
		}

		result = vkMapMemory(device, pixelMemory, 0, pixelAllocationSize, 0, &mappedPixels);
		if (result != VK_SUCCESS) {
			return failVk("vkMapMemory", result);
		}

		return true;
	}

	bool createDescriptorSetLayout() {
		std::array<VkDescriptorSetLayoutBinding, kTotalBindings> bindings{};
		for (uint32_t i = 0; i < bindings.size(); ++i) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateDescriptorSetLayout", result);
		}

		return true;
	}

	bool createDescriptorPoolAndSet() {
		if (descriptorSetLayout == VK_NULL_HANDLE) {
			return fail("Vulkan descriptor set layout missing");
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSize.descriptorCount = kTotalBindings;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;

		VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateDescriptorPool", result);
		}

		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &descriptorSetLayout;

		result = vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet);
		if (result != VK_SUCCESS) {
			return failVk("vkAllocateDescriptorSets", result);
		}

		std::array<VkDescriptorBufferInfo, kTotalBindings> bufferInfos{};
		std::array<VkWriteDescriptorSet, kTotalBindings> writes{};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			if (binding == kBindingPixels) {
				bufferInfos[binding].buffer = pixelBuffer;
				bufferInfos[binding].range = static_cast<VkDeviceSize>(pixelBufferSize);
			}
			else if (binding == kBindingAccum) {
				bufferInfos[binding].buffer = accumBuffer;
				bufferInfos[binding].range = static_cast<VkDeviceSize>(accumBufferSize);
			}
			else if (binding == kBindingSettings) {
				bufferInfos[binding].buffer = settingsBuffer;
				bufferInfos[binding].range = static_cast<VkDeviceSize>(settingsBufferSize);
			}
			else {
				const StorageBuffer& buffer = descriptorBufferForBinding(binding);
				bufferInfos[binding].buffer = buffer.buffer;
				bufferInfos[binding].range = static_cast<VkDeviceSize>(buffer.size);
			}
			bufferInfos[binding].offset = 0;

			writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[binding].dstSet = descriptorSet;
			writes[binding].dstBinding = binding;
			writes[binding].descriptorCount = 1;
			writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[binding].pBufferInfo = &bufferInfos[binding];
		}

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		return true;
	}

	bool createPipeline() {
		const ShaderEntry& entry = kShaders[selectedShader < kShaderCount ? selectedShader : 0];
		VkShaderModuleCreateInfo shaderInfo{};
		shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderInfo.codeSize = entry.len;
		shaderInfo.pCode = reinterpret_cast<const uint32_t*>(entry.spv);

		VkResult result = vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateShaderModule", result);
		}

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &descriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
		if (result != VK_SUCCESS) {
			return failVk("vkCreatePipelineLayout", result);
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = pipelineLayout;

		result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateComputePipelines", result);
		}

		return true;
	}

	bool createCommandResources() {
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamily;

		VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateCommandPool", result);
		}

		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		result = vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);
		if (result != VK_SUCCESS) {
			return failVk("vkAllocateCommandBuffers", result);
		}

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		result = vkCreateFence(device, &fenceInfo, nullptr, &fence);
		if (result != VK_SUCCESS) {
			return failVk("vkCreateFence", result);
		}

		if (timestampSupported) {
			VkQueryPoolCreateInfo queryPoolInfo{};
			queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryPoolInfo.queryCount = 2;
			result = vkCreateQueryPool(device, &queryPoolInfo, nullptr, &timestampPool);
			if (result != VK_SUCCESS) {
				timestampSupported = false;
			}
		}

		return true;
	}

	bool switchPipeline(int index) {
		if (!initialized || index < 0 || index >= kShaderCount) {
			return false;
		}
		if (kShaders[index].requiresModel && !modelBuffersReady) {
			status = "Cannot select glTF model preview\n" + modelScene.status;
			return false;
		}
		if (index == selectedShader) {
			return true;
		}

		vkDeviceWaitIdle(device);

		if (pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
		if (shaderModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, shaderModule, nullptr);
			shaderModule = VK_NULL_HANDLE;
		}

		selectedShader = index;
		if (!createPipeline()) {
			initialized = false;
			return false;
		}

		frameCount = 0;
		lastGpuMs = 0.0;
		setActiveStatus();
		return true;
	}

	bool switchModel(int index) {
		if (!initialized || index < 0 || index >= kModelCount) {
			return false;
		}
		if (index == selectedModel) {
			return true;
		}

		VkResult result = vkDeviceWaitIdle(device);
		if (result != VK_SUCCESS) {
			return failVk("vkDeviceWaitIdle model switch", result);
		}

		destroyDescriptorPool();
		selectedModel = index;
		if (!loadModelSceneResources() || !createDescriptorPoolAndSet()) {
			destroyDescriptorPool();
			modelBuffersReady = false;
			sceneCounts = glm::uvec4(0u);
			return false;
		}

		resetAccumState();
		frameCount = 0;
		lastGpuMs = 0.0;
		setActiveStatus();
		return modelBuffersReady;
	}
};

VulkanComputePreview::VulkanComputePreview()
	: m_impl(std::make_unique<Impl>()) {
}

VulkanComputePreview::~VulkanComputePreview() {
	shutdown();
}

bool VulkanComputePreview::initialize(int width, int height) {
	return m_impl->initialize(width, height);
}

bool VulkanComputePreview::resize(int width, int height) {
	return m_impl->resize(width, height);
}

bool VulkanComputePreview::render(float timeSeconds, const VulkanPreviewCamera& camera, const VulkanPreviewSettings& settings, std::vector<RenderPixel>& pixels) {
	return m_impl->render(timeSeconds, camera, settings, pixels);
}

uint32_t VulkanComputePreview::samplesAccumulated() const {
	return m_impl->currentSample;
}

bool VulkanComputePreview::converged() const {
	return m_impl->currentSample >= m_impl->maxSamplesCached;
}

void VulkanComputePreview::shutdown() {
	m_impl->shutdown();
}

bool VulkanComputePreview::isAvailable() const {
	return m_impl->initialized;
}

int VulkanComputePreview::width() const {
	return m_impl->renderWidth;
}

int VulkanComputePreview::height() const {
	return m_impl->renderHeight;
}

const std::string& VulkanComputePreview::statusMessage() const {
	return m_impl->status;
}

bool VulkanComputePreview::setShader(int index) {
	return m_impl->switchPipeline(index);
}

int VulkanComputePreview::shaderIndex() const {
	return m_impl->selectedShader;
}

int VulkanComputePreview::shaderCount() {
	return kShaderCount;
}

const char* VulkanComputePreview::shaderName(int index) {
	if (index < 0 || index >= kShaderCount) return "Unknown";
	return kShaders[index].name;
}

bool VulkanComputePreview::isModelPreviewIndex(int index) {
	if (index < 0 || index >= kShaderCount) return false;
	return kShaders[index].requiresModel;
}

bool VulkanComputePreview::setModel(int index) {
	return m_impl->switchModel(index);
}

int VulkanComputePreview::modelIndex() const {
	return m_impl->selectedModel;
}

int VulkanComputePreview::modelCount() {
	return kModelCount;
}

const char* VulkanComputePreview::modelName(int index) {
	if (index < 0 || index >= kModelCount) return "Unknown";
	return kModels[index].name;
}

bool VulkanComputePreview::modelBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const {
	if (!m_impl->modelBuffersReady) {
		return false;
	}

	boundsMin = m_impl->modelScene.boundsMin;
	boundsMax = m_impl->modelScene.boundsMax;
	return true;
}

GpuStats VulkanComputePreview::gpuStats() const {
	GpuStats s{};
	s.gpuDispatchMs = m_impl->lastGpuMs;
	s.frameCount = m_impl->frameCount;
	s.localHeapBytes = m_impl->localHeapBytes;
	s.localHeapUsed = m_impl->localHeapUsed;
	s.pixelBufferBytes = m_impl->pixelBufferSize;
	s.memBudgetAvailable = m_impl->memBudgetSupported;
	s.timestampAvailable = m_impl->timestampSupported;
	s.samplesAccumulated = m_impl->currentSample;
	s.maxSamples = m_impl->maxSamplesCached;
	return s;
}
