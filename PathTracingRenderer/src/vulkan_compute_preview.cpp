#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <vulkan_compute_preview.h>

#include <algorithm>
#include <cstring>
#include <sstream>

#include "vulkan_triangle_spv.h"
#include "tut28_star_nest_comp_spv.h"
#include "tut28_lets_self_reflect_comp_spv.h"
#include "tut28_spiral_galaxy_comp_spv.h"
#include "tut28_battered_alien_planet_comp_spv.h"
#include "tut28_flux_core_comp_spv.h"

namespace {
struct PushConstants {
	int width = 0;
	int height = 0;
	float time = 0.0f;
	float padding0 = 0.0f;
};

static_assert(sizeof(PushConstants) == 16, "Push constants must match vulkan_triangle.comp.");
static_assert(nray_vulkan_triangle_comp_spv_len % 4 == 0, "SPIR-V bytecode size must be a multiple of 4.");

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

struct ShaderEntry {
	const char* name;
	const unsigned char* spv;
	unsigned int len;
};

static const ShaderEntry kShaders[] = {
	{ "Hello World Triangle",       nray_vulkan_triangle_comp_spv,          nray_vulkan_triangle_comp_spv_len          },
	{ "Star Nest",                  tut28_star_nest_comp_spv,               tut28_star_nest_comp_spv_len               },
	{ "Lets Self Reflect",          tut28_lets_self_reflect_comp_spv,       tut28_lets_self_reflect_comp_spv_len       },
	{ "Spiral Galaxy",              tut28_spiral_galaxy_comp_spv,           tut28_spiral_galaxy_comp_spv_len           },
	{ "Battered Alien Planet",      tut28_battered_alien_planet_comp_spv,   tut28_battered_alien_planet_comp_spv_len   },
	{ "Flux Core",                  tut28_flux_core_comp_spv,               tut28_flux_core_comp_spv_len               },
};
static const int kShaderCount = static_cast<int>(sizeof(kShaders) / sizeof(kShaders[0]));

}

struct VulkanComputePreview::Impl {
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

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkShaderModule shaderModule = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

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
		status = "Vulkan compute preview active on " + deviceName;
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

		renderWidth = width;
		renderHeight = height;
		if (!createPixelBuffer() || !createDescriptorPoolAndSet()) {
			destroyDescriptorPool();
			destroyPixelBuffer();
			initialized = false;
			return false;
		}

		status = "Vulkan compute preview active on " + deviceName;
		return true;
	}

	bool render(float timeSeconds, std::vector<RenderPixel>& pixels) {
		if (!initialized) {
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

		PushConstants pushConstants{
			renderWidth,
			renderHeight,
			timeSeconds,
			0.0f
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
		memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT,
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
		destroyPixelBuffer();
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
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
			localHeapIndex = UINT32_MAX;
			localHeapBytes = 0;
			for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
				if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
					if (localHeapIndex == UINT32_MAX || memProps.memoryHeaps[i].size > localHeapBytes) {
						localHeapIndex = i;
						localHeapBytes = memProps.memoryHeaps[i].size;
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
		VkPhysicalDeviceMemoryProperties memoryProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProps);

		for (uint32_t i = 0; i < memoryProps.memoryTypeCount; i++) {
			bool typeSupported = (typeBits & (1u << i)) != 0;
			bool flagsSupported = (memoryProps.memoryTypes[i].propertyFlags & preferredFlags) == preferredFlags;
			if (typeSupported && flagsSupported) {
				memoryTypeIndex = i;
				return true;
			}
		}

		return false;
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
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

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
		poolSize.descriptorCount = 1;

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

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = pixelBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = static_cast<VkDeviceSize>(pixelBufferSize);

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = descriptorSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
		return true;
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

bool VulkanComputePreview::render(float timeSeconds, std::vector<RenderPixel>& pixels) {
	return m_impl->render(timeSeconds, pixels);
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

GpuStats VulkanComputePreview::gpuStats() const {
	GpuStats s{};
	s.gpuDispatchMs = m_impl->lastGpuMs;
	s.frameCount = m_impl->frameCount;
	s.localHeapBytes = m_impl->localHeapBytes;
	s.localHeapUsed = m_impl->localHeapUsed;
	s.pixelBufferBytes = m_impl->pixelBufferSize;
	s.memBudgetAvailable = m_impl->memBudgetSupported;
	s.timestampAvailable = m_impl->timestampSupported;
	return s;
}
