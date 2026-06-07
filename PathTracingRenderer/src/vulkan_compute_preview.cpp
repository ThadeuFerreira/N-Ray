#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <gltf_scene.h>
#include <vulkan_compute_preview.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include <json.hpp>

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
	glm::vec4 aTangent;
	glm::vec4 bTangent;
	glm::vec4 cTangent;
	glm::vec4 uv01;
	glm::vec4 uv2;
	glm::uvec4 ids;
};

struct GpuMaterial {
	glm::vec4 baseColor;      // rgba baseColorFactor
	glm::vec4 params;         // roughness, metalness, emissionIntensity, transmission
	glm::vec4 emissionIor;    // rgb emissiveFactor, a IOR
	glm::uvec4 textureIndices; // baseColor, metallicRoughness, normal, emissive
	glm::uvec4 textureInfo;    // occlusion, alphaMode, unused, unused
	glm::vec4 textureParams;   // alphaCutoff, normalScale, occlusionStrength, unused
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
static_assert(sizeof(GpuTriShading) == 144, "GpuTriShading must match std430 shader layout.");
static_assert(sizeof(GpuMaterial) == 96, "GpuMaterial must match std430 shader layout.");
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
	std::string name;       // display name (folder name, or overridden in settings)
	std::string scenePath;  // resolved .gltf/.glb scene file (generic absolute path)
	std::string folderPath; // canonical model folder, persisted to project_settings.json
	std::string folderKey;  // normalized folder path used for dedup comparison
};

static const ShaderEntry kShaders[] = {
	{ "Hello World Triangle",       nray_vulkan_triangle_comp_spv,          nray_vulkan_triangle_comp_spv_len,          false },
	{ "Star Nest",                  tut28_star_nest_comp_spv,               tut28_star_nest_comp_spv_len,               false },
	{ "Lets Self Reflect",          tut28_lets_self_reflect_comp_spv,       tut28_lets_self_reflect_comp_spv_len,       false },
	{ "Spiral Galaxy",              tut28_spiral_galaxy_comp_spv,           tut28_spiral_galaxy_comp_spv_len,           false },
	{ "Battered Alien Planet",      tut28_battered_alien_planet_comp_spv,   tut28_battered_alien_planet_comp_spv_len,   false },
	{ "Flux Core",                  tut28_flux_core_comp_spv,               tut28_flux_core_comp_spv_len,               false },
	{ "glTF Model Preview",         nray_vulkan_gltf_flat_comp_spv,         nray_vulkan_gltf_flat_comp_spv_len,         true  },
};
static const int kShaderCount = static_cast<int>(sizeof(kShaders) / sizeof(kShaders[0]));

static constexpr uint32_t kMaxPreviewTextures = 256;
// Total descriptor set bindings: 0=pixels, 1-4=scene SSBOs, 5=accum, 6=settings, 7=textures.
static constexpr uint32_t kTotalBindings = 8;
static constexpr uint32_t kStorageBindings = 7;
// Scene SSBO bindings start at index 1 (binding 0 is the pixel buffer).
static constexpr uint32_t kFirstSceneBinding = 1;
// Named binding slots — update if the layout in vulkan_gltf_flat.comp changes.
static constexpr uint32_t kBindingPixels   = 0;
static constexpr uint32_t kBindingAccum    = 5;
static constexpr uint32_t kBindingSettings = 6;
static constexpr uint32_t kBindingTextures = 7;

// Imported models are persisted here (relative to the working directory, which
// is the PathTracingRenderer/ folder where the app is run) so they reappear on
// the next run without re-importing.
static const char* kProjectSettingsFile = "project_settings.json";

void logModelImport(const std::string& message) {
	std::cerr << "[VulkanModelImport] " << message << '\n';
}

std::string toLowerAscii(std::string value) {
	for (char& c : value) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return value;
}

bool pathExists(const std::filesystem::path& path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

std::filesystem::path absoluteLexicallyNormal(std::filesystem::path path) {
	std::error_code ec;
	std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
	if (!ec) {
		path = absolutePath;
	}
	return path.lexically_normal();
}

std::filesystem::path canonicalOrAbsolute(std::filesystem::path path) {
	std::error_code ec;
	std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
	if (!ec) {
		return canonicalPath;
	}
	return absoluteLexicallyNormal(path);
}

std::string trimAscii(std::string text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
		text.erase(text.begin());
	}
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
		text.pop_back();
	}
	return text;
}

// Resolve a (possibly relative) folder against the working directory and its
// parent (assets live at the repo root, the app runs from PathTracingRenderer/).
// Returns the canonical folder path if an existing directory is found.
std::optional<std::filesystem::path> resolveExistingFolder(const std::filesystem::path& input) {
	std::error_code ec;
	if (input.is_absolute()) {
		if (pathExists(input) && std::filesystem::is_directory(input, ec) && !ec) {
			return canonicalOrAbsolute(input);
		}
		return std::nullopt;
	}

	std::array<std::filesystem::path, 2> candidates{
		input,
		std::filesystem::path("..") / input
	};
	for (const std::filesystem::path& candidate : candidates) {
		if (pathExists(candidate) && std::filesystem::is_directory(candidate, ec) && !ec) {
			return canonicalOrAbsolute(candidate);
		}
	}
	return std::nullopt;
}

std::string genericPathString(const std::filesystem::path& path) {
	std::string text = path.generic_string();
	for (char& c : text) {
		if (c == '\\') {
			c = '/';
		}
	}
	return text;
}

std::string normalizePathKey(std::filesystem::path path) {
	path = canonicalOrAbsolute(path);
	std::string text = genericPathString(path);

#ifdef _WIN32
	text = toLowerAscii(text);
#endif
	return text;
}

std::string sceneFileNameLower(const std::filesystem::path& path) {
	return toLowerAscii(path.filename().string());
}

std::optional<std::filesystem::path> findSceneFileInFolder(const std::filesystem::path& folderPath) {
	std::error_code ec;
	if (!std::filesystem::exists(folderPath, ec) || ec) {
		return std::nullopt;
	}

	std::optional<std::filesystem::path> sceneFile;
	std::optional<std::filesystem::path> fallbackSceneFile;
	std::error_code iterEc;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folderPath, iterEc)) {
		if (iterEc) {
			return std::nullopt;
		}
		if (!entry.is_regular_file()) {
			continue;
		}

		const std::string ext = toLowerAscii(entry.path().extension().string());
		if (ext != ".gltf" && ext != ".glb") {
			continue;
		}

		std::string fileName = sceneFileNameLower(entry.path());
		if (fileName == "scene.gltf" && !sceneFile.has_value()) {
			sceneFile = entry.path();
			break;
		}
		if (!fallbackSceneFile.has_value()) {
			fallbackSceneFile = entry.path();
		}
	}

	if (sceneFile.has_value()) {
		return sceneFile;
	}

	return fallbackSceneFile;
}

// Build a ModelEntry from an existing (already resolved) folder by locating its
// .gltf/.glb scene file. Returns nullopt when the folder has no scene file.
std::optional<ModelEntry> buildModelEntryForFolder(const std::filesystem::path& folder) {
	std::optional<std::filesystem::path> sceneFile = findSceneFileInFolder(folder);
	if (!sceneFile.has_value()) {
		return std::nullopt;
	}

	std::string folderName = folder.filename().string();
	if (folderName.empty()) {
		folderName = folder.string();
	}
	if (folderName.empty()) {
		folderName = "Imported glTF Model";
	}

	ModelEntry entry;
	entry.name = folderName;
	entry.scenePath = genericPathString(canonicalOrAbsolute(*sceneFile));
	entry.folderPath = genericPathString(folder);
	entry.folderKey = normalizePathKey(folder);
	return entry;
}

void saveModelEntriesToSettings(const std::vector<ModelEntry>& entries) {
	nlohmann::json root;
	root["models"] = nlohmann::json::array();
	for (const ModelEntry& entry : entries) {
		nlohmann::json item;
		item["name"] = entry.name;
		item["folder"] = entry.folderPath;
		root["models"].push_back(item);
	}

	std::ofstream out(kProjectSettingsFile, std::ios::trunc);
	if (!out.is_open()) {
		logModelImport(std::string("failed to write ") + kProjectSettingsFile);
		return;
	}
	out << root.dump(4) << '\n';
	logModelImport("saved " + std::to_string(entries.size()) + " model(s) to " + kProjectSettingsFile);
}

std::vector<ModelEntry> loadModelEntriesFromSettings() {
	std::vector<ModelEntry> entries;

	std::error_code ec;
	if (!std::filesystem::exists(kProjectSettingsFile, ec) || ec) {
		return entries;
	}

	std::ifstream in(kProjectSettingsFile);
	if (!in.is_open()) {
		return entries;
	}

	nlohmann::json root;
	try {
		in >> root;
	}
	catch (const std::exception& e) {
		logModelImport(std::string("failed to parse ") + kProjectSettingsFile + ": " + e.what());
		return entries;
	}

	if (!root.contains("models") || !root["models"].is_array()) {
		return entries;
	}

	for (const nlohmann::json& item : root["models"]) {
		std::string folder;
		std::string overrideName;
		if (item.is_string()) {
			folder = item.get<std::string>();
		}
		else if (item.is_object()) {
			if (item.contains("folder") && item["folder"].is_string()) {
				folder = item["folder"].get<std::string>();
			}
			if (item.contains("name") && item["name"].is_string()) {
				overrideName = item["name"].get<std::string>();
			}
		}

		folder = trimAscii(folder);
		if (folder.empty()) {
			continue;
		}

		std::optional<std::filesystem::path> resolved = resolveExistingFolder(folder);
		if (!resolved.has_value()) {
			logModelImport("settings model folder missing, skipping: " + folder);
			continue;
		}

		std::optional<ModelEntry> entry = buildModelEntryForFolder(*resolved);
		if (!entry.has_value()) {
			logModelImport("settings model folder has no .gltf/.glb, skipping: " + folder);
			continue;
		}
		if (!overrideName.empty()) {
			entry->name = overrideName;
		}

		bool duplicate = false;
		for (const ModelEntry& existing : entries) {
			if (existing.folderKey == entry->folderKey) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			entries.push_back(std::move(*entry));
		}
	}

	logModelImport("loaded " + std::to_string(entries.size()) + " model(s) from " + kProjectSettingsFile);
	return entries;
}

std::vector<ModelEntry>& activeModelEntries() {
	static std::vector<ModelEntry> entries = loadModelEntriesFromSettings();
	return entries;
}

}

struct VulkanComputePreview::Impl {
	struct StorageBuffer {
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceSize allocationSize = 0;
		size_t size = 0;
		bool memoryCoherent = false;
	};

	struct TextureResource {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		VkDescriptorImageInfo descriptor{};
		uint32_t width = 1;
		uint32_t height = 1;
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
	std::vector<TextureResource> textureResources;
	std::vector<VkDescriptorImageInfo> textureDescriptorInfos;
	uint32_t textureDescriptorCount = 1;
	bool intelGpu = false;
	GltfPreviewScene modelScene;
	bool modelBuffersReady = false;
	// Host-side mirror of the SCENE_MATERIAL SSBO. `materialsOriginal` is the
	// as-imported pack; `materialsCurrent` carries live UI overrides. Both are
	// repopulated on every model load and cleared when buffers are torn down.
	std::vector<GpuMaterial> materialsOriginal;
	std::vector<GpuMaterial> materialsCurrent;
	// triCount, bvhNodeCount, materialCount, textureCount — cached once at load so
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
	bool descriptorIndexingSupported = false;
	uint64_t localHeapBytes = 0;
	uint32_t localHeapIndex = UINT32_MAX;
	uint64_t localHeapUsed = 0;
	double lastGpuMs = 0.0;
	uint32_t frameCount = 0;

	int selectedShader = 0;
	int selectedModel = -1;
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
			!createCommandResources() ||
			!createPixelBuffer() ||
			!createDummySceneBuffer() ||
			!loadModelSceneResources() ||
			!createAccumBuffer() ||
			!createSettingsBuffer() ||
			!createDescriptorSetLayout() ||
			!createDescriptorPoolAndSet() ||
			!createPipeline()) {
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
		if (selectedEntry.requiresModel && !modelBuffersReady) {
			status = "Cannot render glTF model preview: no model buffers are ready";
			if (!modelScene.status.empty()) {
				status += "\n";
				status += modelScene.status;
			}
			return false;
		}

		// The [kMinSamples, kMaxSamples] policy is enforced upstream by the driver
		// and UI; here we only guard against a non-positive dispatch count.
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
		// On coherent memory (always true on Intel UMA) the next frame's sample barrier
		// handles COMPUTE→COMPUTE ordering; only HOST visibility is needed here.
		// On non-coherent memory keep the conservative mask.
		VkPipelineStageFlags finalDstStage;
		if (pixelMemoryCoherent) {
			memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
			finalDstStage = VK_PIPELINE_STAGE_HOST_BIT;
		} else {
			memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			finalDstStage = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			finalDstStage,
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
		destroyTextureResources();
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
		materialsOriginal.clear();
		materialsCurrent.clear();
		sceneCounts = glm::uvec4(0u);
		textureDescriptorCount = 1;
		initialized = false;
		lastGpuMs = 0.0;
		localHeapUsed = 0;
		frameCount = 0;
		timestampSupported = false;
		memBudgetSupported = false;
		descriptorIndexingSupported = false;
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

	// Re-upload the host material mirror into the (host-visible) SCENE_MATERIAL
	// SSBO. Safe to call between frames: render() fully fences each dispatch, so
	// the GPU is not reading the buffer here.
	bool updateMaterialBuffer() {
		StorageBuffer& buffer = sceneBuffers[SCENE_MATERIAL];
		if (buffer.memory == VK_NULL_HANDLE || materialsCurrent.empty()) {
			return false;
		}
		size_t byteSize = std::min(materialsCurrent.size() * sizeof(GpuMaterial), buffer.size);
		void* mapped = nullptr;
		VkResult result = vkMapMemory(device, buffer.memory, 0, buffer.allocationSize, 0, &mapped);
		if (result != VK_SUCCESS) {
			return failVk("vkMapMemory material update", result);
		}
		std::memcpy(mapped, materialsCurrent.data(), byteSize);
		bool flushed = flushMappedRange(buffer.memory, buffer.memoryCoherent, "vkFlushMappedMemoryRanges material update");
		vkUnmapMemory(device, buffer.memory);
		return flushed;
	}

	bool setMaterialState(int index, const VulkanPreviewMaterialState& state) {
		if (index < 0 || index >= static_cast<int>(materialsCurrent.size())) {
			return false;
		}
		GpuMaterial& material = materialsCurrent[static_cast<size_t>(index)];
		material.baseColor = glm::vec4(state.baseColor, material.baseColor.w);
		material.params.x = std::clamp(state.roughness, 0.0f, 1.0f);
		material.params.y = std::clamp(state.metalness, 0.0f, 1.0f);
		if (!updateMaterialBuffer()) {
			return false;
		}
		resetAccumState();
		return true;
	}

	void resetMaterialStates() {
		if (materialsOriginal.empty() || materialsCurrent.size() != materialsOriginal.size()) {
			return;
		}
		materialsCurrent = materialsOriginal;
		if (updateMaterialBuffer()) {
			resetAccumState();
		}
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
		intelGpu = (selectedProps.vendorID == 0x8086);

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
		descriptorIndexingSupported = false;
		{
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> exts(extCount);
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, exts.data());
			for (const auto& ext : exts) {
				if (strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
					memBudgetSupported = true;
				}
				if (strcmp(ext.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
					descriptorIndexingSupported = true;
				}
			}
		}

		if (!descriptorIndexingSupported) {
			return fail("Vulkan preview disabled: VK_EXT_descriptor_indexing is required for glTF texture arrays");
		}

		VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures{};
		descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
		VkPhysicalDeviceFeatures2 availableFeatures{};
		availableFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		availableFeatures.pNext = &descriptorIndexingFeatures;
		vkGetPhysicalDeviceFeatures2(physicalDevice, &availableFeatures);
		if (descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing != VK_TRUE) {
			return fail("Vulkan preview disabled: non-uniform sampled-image indexing is required for glTF textures");
		}

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physicalDevice, &props);
		if (props.limits.maxPerStageDescriptorSampledImages < kMaxPreviewTextures ||
			props.limits.maxDescriptorSetSampledImages < kMaxPreviewTextures) {
			return fail("Vulkan preview disabled: device sampled-image descriptor limit is too low for the glTF texture array");
		}

		VkPhysicalDeviceDescriptorIndexingFeaturesEXT enabledDescriptorIndexing{};
		enabledDescriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
		enabledDescriptorIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		VkPhysicalDeviceFeatures2 enabledFeatures{};
		enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		enabledFeatures.pNext = &enabledDescriptorIndexing;

		std::vector<const char*> enabledExtensions;
		enabledExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		if (memBudgetSupported) {
			enabledExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
		}

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = &enabledFeatures;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();

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

	VkSamplerAddressMode samplerAddressMode(int wrapMode) const {
		switch (wrapMode) {
		case 33071: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case 10497:
		default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		}
	}

	VkFilter samplerFilter(int filterMode) const {
		return filterMode == 9728 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	}

	VkSamplerMipmapMode samplerMipmapMode(int minFilterMode) const {
		return (minFilterMode == 9986 || minFilterMode == 9987)
			? VK_SAMPLER_MIPMAP_MODE_LINEAR
			: VK_SAMPLER_MIPMAP_MODE_NEAREST;
	}

	GltfPreviewTexture solidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char* name) const {
		GltfPreviewTexture texture;
		texture.name = name;
		texture.width = 1;
		texture.height = 1;
		texture.rgba = { r, g, b, a };
		return texture;
	}

	void destroyTextureResource(TextureResource& texture) {
		if (texture.sampler != VK_NULL_HANDLE) {
			vkDestroySampler(device, texture.sampler, nullptr);
			texture.sampler = VK_NULL_HANDLE;
		}
		if (texture.view != VK_NULL_HANDLE) {
			vkDestroyImageView(device, texture.view, nullptr);
			texture.view = VK_NULL_HANDLE;
		}
		if (texture.image != VK_NULL_HANDLE) {
			vkDestroyImage(device, texture.image, nullptr);
			texture.image = VK_NULL_HANDLE;
		}
		if (texture.memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, texture.memory, nullptr);
			texture.memory = VK_NULL_HANDLE;
		}
		texture.descriptor = {};
		texture.width = 1;
		texture.height = 1;
	}

	void destroyTextureResources() {
		for (TextureResource& texture : textureResources) {
			destroyTextureResource(texture);
		}
		textureResources.clear();
		textureDescriptorInfos.clear();
		textureDescriptorCount = 1;
	}

	bool createBufferResource(
		VkDeviceSize byteSize,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags memoryFlags,
		VkBuffer& buffer,
		VkDeviceMemory& memory,
		VkDeviceSize& allocationSize,
		const char* tag
	) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = byteSize;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkCreateBuffer ") + tag).c_str(), result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetBufferMemoryRequirements(device, buffer, &memoryReqs);

		uint32_t memoryTypeIndex = 0;
		if (!findMemoryType(memoryReqs.memoryTypeBits, memoryFlags, memoryTypeIndex)) {
			return fail(std::string("No matching memory type for Vulkan ") + tag + " buffer");
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

		return true;
	}

	void destroyTransientBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
		if (buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
		}
		if (memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, memory, nullptr);
			memory = VK_NULL_HANDLE;
		}
	}

	bool submitImmediate(const char* tag, const std::function<void(VkCommandBuffer)>& record) {
		if (commandPool == VK_NULL_HANDLE) {
			return fail(std::string("Vulkan command pool missing for ") + tag);
		}

		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
		VkResult result = vkAllocateCommandBuffers(device, &allocateInfo, &uploadCommandBuffer);
		if (result != VK_SUCCESS) {
			return failVk((std::string("vkAllocateCommandBuffers ") + tag).c_str(), result);
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(uploadCommandBuffer, &beginInfo);
		if (result != VK_SUCCESS) {
			vkFreeCommandBuffers(device, commandPool, 1, &uploadCommandBuffer);
			return failVk((std::string("vkBeginCommandBuffer ") + tag).c_str(), result);
		}

		record(uploadCommandBuffer);

		result = vkEndCommandBuffer(uploadCommandBuffer);
		if (result != VK_SUCCESS) {
			vkFreeCommandBuffers(device, commandPool, 1, &uploadCommandBuffer);
			return failVk((std::string("vkEndCommandBuffer ") + tag).c_str(), result);
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &uploadCommandBuffer;
		result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
		if (result == VK_SUCCESS) {
			result = vkQueueWaitIdle(queue);
		}
		vkFreeCommandBuffers(device, commandPool, 1, &uploadCommandBuffer);
		if (result != VK_SUCCESS) {
			return failVk((std::string("Vulkan immediate submit ") + tag).c_str(), result);
		}
		return true;
	}

	bool createTextureResource(const GltfPreviewTexture& source, TextureResource& texture) {
		destroyTextureResource(texture);

		uint32_t width = std::max(source.width, 1u);
		uint32_t height = std::max(source.height, 1u);
		size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
		const std::vector<uint8_t>* uploadBytes = &source.rgba;
		GltfPreviewTexture fallback;
		if (uploadBytes->size() < expectedBytes) {
			fallback = solidTexture(255, 255, 255, 255, "invalid texture fallback");
			width = fallback.width;
			height = fallback.height;
			expectedBytes = fallback.rgba.size();
			uploadBytes = &fallback.rgba;
		}

		std::cout << "Vulkan glTF texture upload";
		if (!source.name.empty()) {
			std::cout << " '" << source.name << "'";
		}
		std::cout << ": " << width << "x" << height << " bytes=" << expectedBytes << std::endl;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		VkDeviceSize stagingAllocationSize = 0;
		if (!createBufferResource(
			static_cast<VkDeviceSize>(expectedBytes),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingMemory,
			stagingAllocationSize,
			"texture staging")) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			return false;
		}

		void* mapped = nullptr;
		VkResult result = vkMapMemory(device, stagingMemory, 0, stagingAllocationSize, 0, &mapped);
		if (result != VK_SUCCESS) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			return failVk("vkMapMemory texture staging", result);
		}
		std::memcpy(mapped, uploadBytes->data(), expectedBytes);
		vkUnmapMemory(device, stagingMemory);

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		result = vkCreateImage(device, &imageInfo, nullptr, &texture.image);
		if (result != VK_SUCCESS) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			return failVk("vkCreateImage texture", result);
		}

		VkMemoryRequirements memoryReqs{};
		vkGetImageMemoryRequirements(device, texture.image, &memoryReqs);
		uint32_t memoryTypeIndex = 0;
		if (!findMemoryType(memoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex)) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			destroyTextureResource(texture);
			return fail("No device-local memory type for Vulkan texture image");
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryReqs.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;
		result = vkAllocateMemory(device, &allocateInfo, nullptr, &texture.memory);
		if (result != VK_SUCCESS) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			destroyTextureResource(texture);
			return failVk("vkAllocateMemory texture", result);
		}

		result = vkBindImageMemory(device, texture.image, texture.memory, 0);
		if (result != VK_SUCCESS) {
			destroyTransientBuffer(stagingBuffer, stagingMemory);
			destroyTextureResource(texture);
			return failVk("vkBindImageMemory texture", result);
		}

		bool submitted = submitImmediate("texture upload", [&](VkCommandBuffer cmd) {
			VkImageSubresourceRange range{};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;

			VkImageMemoryBarrier toTransfer{};
			toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = texture.image;
			toTransfer.subresourceRange = range;
			toTransfer.srcAccessMask = 0;
			toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0,
				nullptr,
				0,
				nullptr,
				1,
				&toTransfer
			);

			VkBufferImageCopy copy{};
			copy.bufferOffset = 0;
			copy.bufferRowLength = 0;
			copy.bufferImageHeight = 0;
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.mipLevel = 0;
			copy.imageSubresource.baseArrayLayer = 0;
			copy.imageSubresource.layerCount = 1;
			copy.imageOffset = { 0, 0, 0 };
			copy.imageExtent = { width, height, 1 };
			vkCmdCopyBufferToImage(cmd, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

			VkImageMemoryBarrier toShader{};
			toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toShader.image = texture.image;
			toShader.subresourceRange = range;
			toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0,
				nullptr,
				0,
				nullptr,
				1,
				&toShader
			);
		});

		destroyTransientBuffer(stagingBuffer, stagingMemory);
		if (!submitted) {
			destroyTextureResource(texture);
			return false;
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = texture.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		result = vkCreateImageView(device, &viewInfo, nullptr, &texture.view);
		if (result != VK_SUCCESS) {
			destroyTextureResource(texture);
			return failVk("vkCreateImageView texture", result);
		}

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = samplerFilter(source.magFilter);
		samplerInfo.minFilter = samplerFilter(source.minFilter);
		samplerInfo.mipmapMode = samplerMipmapMode(source.minFilter);
		samplerInfo.addressModeU = samplerAddressMode(source.wrapS);
		samplerInfo.addressModeV = samplerAddressMode(source.wrapT);
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		result = vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler);
		if (result != VK_SUCCESS) {
			destroyTextureResource(texture);
			return failVk("vkCreateSampler texture", result);
		}

		texture.width = width;
		texture.height = height;
		texture.descriptor.sampler = texture.sampler;
		texture.descriptor.imageView = texture.view;
		texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return true;
	}

	bool rebuildTextureDescriptorInfos() {
		if (textureResources.empty()) {
			return fail("No Vulkan texture descriptors available");
		}

		textureDescriptorCount = static_cast<uint32_t>(std::min<size_t>(textureResources.size(), kMaxPreviewTextures));
		textureDescriptorInfos.assign(kMaxPreviewTextures, textureResources.front().descriptor);
		for (uint32_t i = 0; i < textureDescriptorCount; ++i) {
			textureDescriptorInfos[i] = textureResources[i].descriptor;
		}
		return true;
	}

	bool createFallbackTextureResources() {
		destroyTextureResources();
		textureResources.emplace_back();
		GltfPreviewTexture fallback = solidTexture(255, 255, 255, 255, "fallback white");
		if (!createTextureResource(fallback, textureResources.back())) {
			destroyTextureResources();
			return false;
		}
		return rebuildTextureDescriptorInfos();
	}

	bool createTextureResources(const std::vector<GltfPreviewTexture>& textures) {
		destroyTextureResources();
		if (textures.empty()) {
			return createFallbackTextureResources();
		}

		size_t uploadCount = std::min<size_t>(textures.size(), kMaxPreviewTextures);
		textureResources.resize(uploadCount);
		for (size_t i = 0; i < uploadCount; ++i) {
			if (!createTextureResource(textures[i], textureResources[i])) {
				destroyTextureResources();
				return false;
			}
		}

		return rebuildTextureDescriptorInfos();
	}

	uint32_t gpuTextureIndex(uint32_t textureIndex) const {
		if (textureIndex == GLTF_PREVIEW_INVALID_TEXTURE || textureIndex >= textureDescriptorCount) {
			return GLTF_PREVIEW_INVALID_TEXTURE;
		}
		return textureIndex;
	}

	bool createDummySceneBuffer() {
		uint32_t zero[4] = {};
		return createStorageBuffer(dummyBuffer, zero, sizeof(zero));
	}

	bool loadModelSceneResources() {
		modelBuffersReady = false;
		sceneCounts = glm::uvec4(0u);
		modelScene = {};
		materialsOriginal.clear();
		materialsCurrent.clear();
		for (StorageBuffer& buffer : sceneBuffers) {
			destroyStorageBuffer(buffer);
		}
		destroyTextureResources();

		auto& models = activeModelEntries();
		if (selectedModel < 0 || selectedModel >= static_cast<int>(models.size())) {
			// No model selected (fresh start, or none imported yet). Keep the
			// preview usable for the shader-only modes by creating the fallback
			// texture array the descriptor set requires; model preview becomes
			// available once a model is selected/imported.
			logModelImport("loadModelSceneResources: no model selected; using fallback textures");
			if (!createFallbackTextureResources()) {
				status = "Vulkan fallback texture creation failed";
				return false;
			}
			modelScene = {};
			modelScene.status = models.empty()
				? "No glTF models registered. Import a model folder to enable model preview."
				: "No glTF model selected.";
			return true;
		}

		const int modelIndex = selectedModel;
		const ModelEntry& modelEntry = models[modelIndex];
		{
			std::ostringstream out;
			out << "loading model resources index=" << modelIndex
				<< " name=\"" << modelEntry.name << "\""
				<< " scene=\"" << modelEntry.scenePath << "\"";
			logModelImport(out.str());
		}
		if (!loadGltfPreviewScene(modelEntry.scenePath, modelScene)) {
			if (!modelScene.status.empty()) {
				modelScene.status = modelEntry.name + " (" + modelEntry.scenePath + "): " + modelScene.status;
			}
			status = modelScene.status;
			logModelImport("glTF load failed: " + status);
			return false;
		}
		modelScene.status = modelEntry.name + ": " + modelScene.status;
		{
			std::ostringstream out;
			out << "glTF loaded index=" << modelIndex
				<< " triangles=" << modelScene.tris.size()
				<< " triIsect=" << modelScene.triIsect.size()
				<< " bvhNodes=" << modelScene.flatBvh.size()
				<< " materials=" << modelScene.materials.size()
				<< " textures=" << modelScene.textures.size();
			logModelImport(out.str());
		}
		if (!createTextureResources(modelScene.textures)) {
			std::string textureFailure = status;
			if (!createFallbackTextureResources()) {
				modelScene.status = "glTF model texture upload failed: " + textureFailure;
				status = modelScene.status;
				logModelImport("texture upload failed and fallback texture creation failed: " + status);
				return false;
			}
			modelScene.status += " (texture upload warning: " + textureFailure + "; using fallback textures)";
			logModelImport("texture upload failed; using fallback textures: " + textureFailure);
		}
		else {
			std::ostringstream out;
			out << "textures ready descriptorCount=" << textureDescriptorCount;
			logModelImport(out.str());
		}

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
		for (size_t i = 0; i < modelScene.tris.size(); ++i) {
			const Tri& tri = modelScene.tris[i];
			const GltfPreviewTriSurface surface = i < modelScene.triSurfaces.size()
				? modelScene.triSurfaces[i]
				: GltfPreviewTriSurface{};
			gpuTriShading.push_back({
				glm::vec4(tri.aN, 0.0f),
				glm::vec4(tri.bN, 0.0f),
				glm::vec4(tri.cN, 0.0f),
				surface.aTangent,
				surface.bTangent,
				surface.cTangent,
				glm::vec4(surface.aUv, surface.bUv),
				glm::vec4(surface.cUv, 0.0f, 0.0f),
				glm::uvec4(tri.materialIdx, tri.modelIdx, 0u, 0u)
			});
		}

		std::vector<GpuMaterial> gpuMaterials;
		gpuMaterials.reserve(modelScene.materials.size());
		for (size_t i = 0; i < modelScene.materials.size(); ++i) {
			const PBRMaterial& material = modelScene.materials[i];
			const GltfPreviewMaterialMeta* meta = i < modelScene.materialMeta.size() ? &modelScene.materialMeta[i] : nullptr;
			float transmission = i < modelScene.materialTransmission.size() ? modelScene.materialTransmission[i] : 0.0f;
			glm::vec4 baseColor = meta ? meta->baseColorFactor : glm::vec4(material.albedo, 1.0f);
			glm::vec3 emission = meta ? meta->emissiveFactor : material.emissionCol;
			float roughness = meta ? meta->roughness : material.roughness;
			float metalness = meta ? meta->metalness : material.metalness;
			float emissionIntensity = meta ? meta->emissiveStrength : material.emissionIntensity;
			uint32_t baseColorTexture = meta ? gpuTextureIndex(meta->baseColorTexture) : GLTF_PREVIEW_INVALID_TEXTURE;
			uint32_t metallicRoughnessTexture = meta ? gpuTextureIndex(meta->metallicRoughnessTexture) : GLTF_PREVIEW_INVALID_TEXTURE;
			uint32_t normalTexture = meta ? gpuTextureIndex(meta->normalTexture) : GLTF_PREVIEW_INVALID_TEXTURE;
			uint32_t emissiveTexture = meta ? gpuTextureIndex(meta->emissiveTexture) : GLTF_PREVIEW_INVALID_TEXTURE;
			uint32_t occlusionTexture = meta ? gpuTextureIndex(meta->occlusionTexture) : GLTF_PREVIEW_INVALID_TEXTURE;
			float alphaCutoff = meta ? meta->alphaCutoff : 0.5f;
			float normalScale = meta ? meta->normalScale : 1.0f;
			float occlusionStrength = meta ? meta->occlusionStrength : 1.0f;
			uint32_t alphaMode = meta ? meta->alphaMode : GLTF_PREVIEW_ALPHA_OPAQUE;
			gpuMaterials.push_back({
				baseColor,
				glm::vec4(roughness, metalness, emissionIntensity, transmission),
				glm::vec4(emission, material.IOR),
				glm::uvec4(baseColorTexture, metallicRoughnessTexture, normalTexture, emissiveTexture),
				glm::uvec4(occlusionTexture, alphaMode, 0u, 0u),
				glm::vec4(alphaCutoff, normalScale, occlusionStrength, 0.0f)
			});
		}

		// Keep a host mirror so the UI can override PBR factors live without
		// re-importing the glTF (see setMaterialState/updateMaterialBuffer).
		materialsOriginal = gpuMaterials;
		materialsCurrent = gpuMaterials;

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
			status = modelScene.status;
			logModelImport("scene SSBO upload failed: " + status);
			for (StorageBuffer& buffer : sceneBuffers) {
				destroyStorageBuffer(buffer);
			}
			return false;
		}

		sceneCounts = glm::uvec4(
			static_cast<uint32_t>(modelScene.triIsect.size()),
			static_cast<uint32_t>(modelScene.flatBvh.size()),
			static_cast<uint32_t>(modelScene.materials.size()),
			textureDescriptorCount
		);
		modelBuffersReady = true;
		{
			std::ostringstream out;
			out << "scene buffers ready"
				<< " triIsectBytes=" << sceneBuffers[SCENE_TRI_ISECT].size
				<< " triShadingBytes=" << sceneBuffers[SCENE_TRI_SHADING].size
				<< " materialBytes=" << sceneBuffers[SCENE_MATERIAL].size
				<< " bvhBytes=" << sceneBuffers[SCENE_BVH].size
				<< " sceneCounts=(" << sceneCounts.x << ", " << sceneCounts.y
				<< ", " << sceneCounts.z << ", " << sceneCounts.w << ")";
			logModelImport(out.str());
		}
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
		for (uint32_t i = 0; i < kStorageBindings; ++i) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		bindings[kBindingTextures].binding = kBindingTextures;
		bindings[kBindingTextures].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[kBindingTextures].descriptorCount = kMaxPreviewTextures;
		bindings[kBindingTextures].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
			logModelImport("descriptor set creation failed: descriptor set layout missing");
			return fail("Vulkan descriptor set layout missing");
		}

		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[0].descriptorCount = kStorageBindings;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = kMaxPreviewTextures;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

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

		std::array<VkDescriptorBufferInfo, kStorageBindings> bufferInfos{};
		std::array<VkWriteDescriptorSet, kTotalBindings> writes{};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			if (binding == kBindingTextures) {
				if (textureDescriptorInfos.size() != kMaxPreviewTextures) {
					std::ostringstream out;
					out << "descriptor set creation failed: texture descriptors not ready size="
						<< textureDescriptorInfos.size()
						<< " expected=" << kMaxPreviewTextures;
					logModelImport(out.str());
					return fail("Vulkan texture descriptor array is not ready");
				}
				writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[binding].dstSet = descriptorSet;
				writes[binding].dstBinding = binding;
				writes[binding].descriptorCount = kMaxPreviewTextures;
				writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[binding].pImageInfo = textureDescriptorInfos.data();
				continue;
			}

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
		{
			std::ostringstream out;
			out << "descriptor set ready modelBuffersReady=" << (modelBuffersReady ? "true" : "false")
				<< " sceneCounts=(" << sceneCounts.x << ", " << sceneCounts.y
				<< ", " << sceneCounts.z << ", " << sceneCounts.w << ")";
			logModelImport(out.str());
		}
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

		// BVH_MAX_STACK_DEPTH: 32 on Intel Iris Xe (narrow register file, SAH tree depth ≤ 25),
		// 64 on all other devices (unchanged default matches the shader's constant_id = 0 default).
		int bvhStackDepth = intelGpu ? 32 : 64;
		VkSpecializationMapEntry specEntry{};
		specEntry.constantID = 0;
		specEntry.offset     = 0;
		specEntry.size       = sizeof(int);
		VkSpecializationInfo specInfo{};
		specInfo.mapEntryCount = 1;
		specInfo.pMapEntries   = &specEntry;
		specInfo.dataSize      = sizeof(int);
		specInfo.pData         = &bvhStackDepth;

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";
		stageInfo.pSpecializationInfo = &specInfo;

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
			std::ostringstream out;
			out << "setShader rejected index=" << index
				<< " initialized=" << (initialized ? "true" : "false");
			logModelImport(out.str());
			return false;
		}
		if (kShaders[index].requiresModel && !modelBuffersReady) {
			status = "Cannot select glTF model preview\n" + modelScene.status;
			logModelImport("setShader rejected glTF preview because model buffers are not ready: " + modelScene.status);
			return false;
		}
		if (index == selectedShader) {
			std::ostringstream out;
			out << "setShader no-op index=" << index << " name=\"" << kShaders[index].name << "\"";
			logModelImport(out.str());
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
			logModelImport("setShader failed while creating pipeline: " + status);
			initialized = false;
			return false;
		}

		frameCount = 0;
		lastGpuMs = 0.0;
		setActiveStatus();
		{
			std::ostringstream out;
			out << "setShader activated index=" << selectedShader
				<< " name=\"" << kShaders[selectedShader].name << "\"";
			logModelImport(out.str());
		}
		return true;
	}

	bool switchModel(int index) {
		auto& models = activeModelEntries();
		if (!initialized || index < 0 || index >= static_cast<int>(models.size())) {
			std::ostringstream out;
			out << "setModel rejected index=" << index
				<< " current=" << selectedModel
				<< " initialized=" << (initialized ? "true" : "false")
				<< " modelCount=" << models.size();
			logModelImport(out.str());
			return false;
		}
		if (index == selectedModel) {
			std::ostringstream out;
			out << "setModel no-op index=" << index
				<< " name=\"" << models[index].name << "\"";
			logModelImport(out.str());
			return true;
		}

		{
			std::ostringstream out;
			out << "setModel requested from index=" << selectedModel
				<< " to index=" << index
				<< " name=\"" << models[index].name << "\""
				<< " scene=\"" << models[index].scenePath << "\"";
			logModelImport(out.str());
		}

		VkResult result = vkDeviceWaitIdle(device);
		if (result != VK_SUCCESS) {
			logModelImport("setModel failed waiting for device idle: " + vkErrorMessage("vkDeviceWaitIdle model switch", result));
			return failVk("vkDeviceWaitIdle model switch", result);
		}

		int previousModel = selectedModel;
		destroyDescriptorPool();
		selectedModel = index;
		bool loaded = loadModelSceneResources();
		bool descriptorsReady = loaded && createDescriptorPoolAndSet();
		if (!loaded || !descriptorsReady) {
			std::string failureStatus = loaded ? status : modelScene.status;
			if (failureStatus.empty()) {
				failureStatus = status;
			}
			{
				std::ostringstream out;
				out << "setModel failed index=" << index
					<< " loaded=" << (loaded ? "true" : "false")
					<< " descriptorsReady=" << (descriptorsReady ? "true" : "false")
					<< " status=\"" << failureStatus << "\"";
				logModelImport(out.str());
			}

			destroyDescriptorPool();
			modelBuffersReady = false;
			sceneCounts = glm::uvec4(0u);
			selectedModel = previousModel;

			// Restore the previous selection and rebuild its descriptor set.
			// loadModelSceneResources handles a -1 selection gracefully (fallback
			// textures, no model), so this also recreates a valid descriptor set
			// when there was no previously loaded model — without it the preview
			// would be left with a destroyed descriptor set and crash on dispatch.
			bool restored = loadModelSceneResources() && createDescriptorPoolAndSet();
			{
				std::ostringstream out;
				out << "setModel restore previous index=" << previousModel
					<< " restored=" << (restored ? "true" : "false");
				if (previousModel >= 0 && previousModel < static_cast<int>(models.size())) {
					out << " name=\"" << models[previousModel].name << "\"";
				}
				logModelImport(out.str());
			}
			if (restored) {
				setActiveStatus();
				status += "\nFailed to select model: " + models[index].name;
				if (!failureStatus.empty()) {
					status += "\n";
					status += failureStatus;
				}
			}
			else {
				status = "Failed to select model: " + models[index].name;
				if (!failureStatus.empty()) {
					status += "\n";
					status += failureStatus;
				}
			}
			return false;
		}

		resetAccumState();
		frameCount = 0;
		lastGpuMs = 0.0;
		setActiveStatus();
		{
			std::ostringstream out;
			out << "setModel activated index=" << selectedModel
				<< " name=\"" << models[selectedModel].name << "\""
				<< " triangles=" << sceneCounts.x
				<< " bvhNodes=" << sceneCounts.y
				<< " materials=" << sceneCounts.z
				<< " textures=" << sceneCounts.w;
			logModelImport(out.str());
		}
		return modelBuffersReady;
	}

	int importModelFromFolder(const std::string& folderPath) {
		logModelImport("import folder requested raw=\"" + folderPath + "\"");
		std::string rawPath = trimAscii(folderPath);
		if (rawPath.empty()) {
			status = "Failed to import glTF model folder: path is empty";
			logModelImport(status);
			return -1;
		}

		std::optional<std::filesystem::path> resolved = resolveExistingFolder(rawPath);
		if (!resolved.has_value()) {
			status = "Failed to import glTF model folder: directory not found (" + rawPath + ")";
			logModelImport(status);
			return -1;
		}
		std::filesystem::path folder = *resolved;
		logModelImport("import folder canonical=\"" + genericPathString(folder) + "\"");

		// Already registered? Return its index instead of adding a duplicate.
		std::string normalizedFolder = normalizePathKey(folder);
		auto& models = activeModelEntries();
		for (int i = 0; i < static_cast<int>(models.size()); ++i) {
			if (models[i].folderKey == normalizedFolder) {
				status = "Using existing model folder: " + models[i].name;
				std::ostringstream out;
				out << status << " index=" << i
					<< " scene=\"" << models[i].scenePath << "\"";
				logModelImport(out.str());
				return i;
			}
		}

		std::optional<ModelEntry> entry = buildModelEntryForFolder(folder);
		if (!entry.has_value()) {
			status = "Failed to import glTF model folder: no .gltf/.glb file found (" + rawPath + ")";
			logModelImport(status);
			return -1;
		}

		std::error_code texturesEc;
		bool hasTexturesFolder = std::filesystem::exists(folder / "textures", texturesEc)
			&& std::filesystem::is_directory(folder / "textures", texturesEc) && !texturesEc;

		models.push_back(*entry);
		int importedIndex = static_cast<int>(models.size()) - 1;

		// Persist so the model reappears automatically on the next run.
		saveModelEntriesToSettings(models);

		status = "Imported model folder: " + entry->name + " -> " + entry->scenePath;
		if (!hasTexturesFolder) {
			status += "\nWarning: no textures folder found in imported model folder";
		}
		{
			std::ostringstream out;
			out << "import folder added index=" << importedIndex
				<< " name=\"" << models[importedIndex].name << "\""
				<< " scene=\"" << models[importedIndex].scenePath << "\""
				<< " hasTexturesFolder=" << (hasTexturesFolder ? "true" : "false")
				<< " modelCount=" << models.size();
			logModelImport(out.str());
		}
		return importedIndex;
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

int VulkanComputePreview::modelPreviewShaderIndex() {
	for (int i = 0; i < kShaderCount; ++i) {
		if (kShaders[i].requiresModel) return i;
	}
	return -1;
}

bool VulkanComputePreview::setModel(int index) {
	return m_impl->switchModel(index);
}

int VulkanComputePreview::modelIndex() const {
	return m_impl->selectedModel;
}

int VulkanComputePreview::modelCount() {
	return static_cast<int>(activeModelEntries().size());
}

const char* VulkanComputePreview::modelName(int index) {
	const auto& models = activeModelEntries();
	if (index < 0 || index >= static_cast<int>(models.size())) return "Unknown";
	return models[index].name.c_str();
}

int VulkanComputePreview::importModelFromFolder(const std::string& folderPath) {
	return m_impl->importModelFromFolder(folderPath);
}

bool VulkanComputePreview::modelBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const {
	if (!m_impl->modelBuffersReady) {
		return false;
	}

	boundsMin = m_impl->modelScene.boundsMin;
	boundsMax = m_impl->modelScene.boundsMax;
	return true;
}

int VulkanComputePreview::materialCount() const {
	return m_impl->modelBuffersReady ? static_cast<int>(m_impl->materialsCurrent.size()) : 0;
}

bool VulkanComputePreview::materialState(int index, VulkanPreviewMaterialState& out) const {
	if (!m_impl->modelBuffersReady || index < 0 || index >= static_cast<int>(m_impl->materialsCurrent.size())) {
		return false;
	}
	const GpuMaterial& material = m_impl->materialsCurrent[static_cast<size_t>(index)];
	out.baseColor = glm::vec3(material.baseColor);
	out.roughness = material.params.x;
	out.metalness = material.params.y;
	out.hasBaseColorTexture = material.textureIndices.x != GLTF_PREVIEW_INVALID_TEXTURE;
	out.hasMetallicRoughnessTexture = material.textureIndices.y != GLTF_PREVIEW_INVALID_TEXTURE;
	return true;
}

bool VulkanComputePreview::setMaterialState(int index, const VulkanPreviewMaterialState& state) {
	return m_impl->modelBuffersReady && m_impl->setMaterialState(index, state);
}

void VulkanComputePreview::resetMaterialStates() {
	if (m_impl->modelBuffersReady) {
		m_impl->resetMaterialStates();
	}
}

GpuStats VulkanComputePreview::gpuStats() const {
	GpuStats s{};
	s.gpuDispatchMs = m_impl->lastGpuMs;
	s.frameCount = m_impl->frameCount;
	uint64_t raysPerSample =
		static_cast<uint64_t>(std::max(m_impl->renderWidth, 0)) *
		static_cast<uint64_t>(std::max(m_impl->renderHeight, 0));
	s.primaryRaysTraced = raysPerSample * static_cast<uint64_t>(m_impl->currentSample);
	if (m_impl->lastGpuMs > 0.0) {
		s.primaryRaysPerSec = static_cast<double>(raysPerSample) / (m_impl->lastGpuMs / 1000.0);
	}
	s.localHeapBytes = m_impl->localHeapBytes;
	s.localHeapUsed = m_impl->localHeapUsed;
	s.pixelBufferBytes = m_impl->pixelBufferSize;
	s.memBudgetAvailable = m_impl->memBudgetSupported;
	s.timestampAvailable = m_impl->timestampSupported;
	s.samplesAccumulated = m_impl->currentSample;
	s.maxSamples = m_impl->maxSamplesCached;
	return s;
}
