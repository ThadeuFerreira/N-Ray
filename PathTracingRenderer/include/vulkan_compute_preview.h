#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <render_types.h>

struct GpuStats {
	double gpuDispatchMs = 0.0;
	uint32_t frameCount = 0;
	uint64_t localHeapBytes = 0;
	uint64_t localHeapUsed = 0;
	uint64_t pixelBufferBytes = 0;
	bool memBudgetAvailable = false;
	bool timestampAvailable = false;
};

class VulkanComputePreview {
public:
	VulkanComputePreview();
	~VulkanComputePreview();

	VulkanComputePreview(const VulkanComputePreview&) = delete;
	VulkanComputePreview& operator=(const VulkanComputePreview&) = delete;

	bool initialize(int width, int height);
	bool resize(int width, int height);
	bool render(float timeSeconds, std::vector<RenderPixel>& pixels);
	void shutdown();

	bool isAvailable() const;
	int width() const;
	int height() const;
	const std::string& statusMessage() const;
	GpuStats gpuStats() const;

	bool setShader(int index);
	int shaderIndex() const;
	static int shaderCount();
	static const char* shaderName(int index);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
