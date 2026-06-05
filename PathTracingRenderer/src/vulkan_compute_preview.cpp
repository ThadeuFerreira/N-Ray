#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <vulkan_compute_preview.h>

#include <algorithm>
#include <cstring>
#include <sstream>

#include "vulkan_triangle_spv.h"

namespace {
struct PushConstants {
	int width = 0;
	int height = 0;
	float time = 0.0f;
	float padding0 = 0.0f;
};

static_assert(sizeof(PushConstants) == 16, "Push constants must match vulkan_triangle.comp.");

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

	int renderWidth = 0;
	int renderHeight = 0;
	bool initialized = false;
	bool volkReady = false;
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

		VkResult result = volkInitialize();
		if (result != VK_SUCCESS) {
			status = vkErrorMessage("volkInitialize", result);
			return false;
		}
		volkReady = true;

		if (!createInstance() ||
			!selectPhysicalDevice() ||
			!createDevice() ||
			!createPixelBuffer() ||
			!createDescriptorResources() ||
			!createPipeline() ||
			!createCommandResources()) {
			destroyResources();
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
		return initialize(width, height);
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

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pushConstants);
		vkCmdDispatch(commandBuffer, ceilDiv(static_cast<uint32_t>(renderWidth), 16), ceilDiv(static_cast<uint32_t>(renderHeight), 16), 1);

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
		if (descriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
			descriptorSet = VK_NULL_HANDLE;
		}
		if (descriptorSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			descriptorSetLayout = VK_NULL_HANDLE;
		}
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
		if (device != VK_NULL_HANDLE) {
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
			queue = VK_NULL_HANDLE;
		}
		if (instance != VK_NULL_HANDLE) {
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		if (volkReady) {
			volkFinalize();
			volkReady = false;
		}

		physicalDevice = VK_NULL_HANDLE;
		pixelAllocationSize = 0;
		pixelBufferSize = 0;
		pixelMemoryCoherent = false;
		initialized = false;
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
		appInfo.apiVersion = VK_API_VERSION_1_0;

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
			return failVk("vkEnumeratePhysicalDevices count", result);
		}
		if (deviceCount == 0) {
			return fail("No Vulkan physical devices found");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		if (result != VK_SUCCESS) {
			return failVk("vkEnumeratePhysicalDevices", result);
		}

		for (VkPhysicalDevice candidate : devices) {
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

			for (uint32_t i = 0; i < queueFamilyCount; i++) {
				if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
					physicalDevice = candidate;
					queueFamily = i;

					VkPhysicalDeviceProperties props{};
					vkGetPhysicalDeviceProperties(physicalDevice, &props);
					deviceName = props.deviceName;
					return true;
				}
			}
		}

		return fail("No Vulkan compute queue family found");
	}

	bool createDevice() {
		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;

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

	bool createDescriptorResources() {
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

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSize.descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;

		result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
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
		VkShaderModuleCreateInfo shaderInfo{};
		shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderInfo.codeSize = nray_vulkan_triangle_comp_spv_len;
		shaderInfo.pCode = reinterpret_cast<const uint32_t*>(nray_vulkan_triangle_comp_spv);

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
