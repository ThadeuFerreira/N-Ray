#pragma once

#include <memory>
#include <string>
#include <vector>

#include <render_types.h>

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

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
