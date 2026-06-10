#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vulkan_compute_preview.h>

struct NrayPerformanceReport {
	bool success = false;
	std::string status;
	std::string generatedAtUtc;

	std::string renderer = "Vulkan Compute Preview";
	std::string engineCommit;
	std::string engineBranch;
	bool engineDirty = false;

	std::string buildConfig;
	std::string compiler;
	std::string platform;
	std::string cpuName;
	uint32_t logicalCores = 0;
	uint64_t systemMemoryBytes = 0;

	int width = 0;
	int height = 0;
	int shaderIndex = -1;
	std::string shaderName;
	int modelIndex = -1;
	std::string modelName;
	int skyIndex = -1;
	std::string skyName;

	int samplesRequested = 0;
	int samplesAccumulated = 0;
	bool converged = false;
	int raysPerPixel = 1;
	int maxBounces = 0;
	int rrMinBounces = 0;
	bool russianRoulette = true;
	float exposure = 1.0f;
	float contrast = 0.8f;
	std::string shadowMode;
	std::string denoiserMode;
	std::string denoiserDebugView;

	bool environmentEnabled = false;
	bool skyEnabled = false;
	bool sunEnabled = false;
	bool threePointEnabled = false;
	bool keyLightEnabled = false;
	bool fillLightEnabled = false;
	bool rimLightEnabled = false;
	bool pointLightShadows = false;   // global shadow master (sun + key/fill/rim)
	bool keyLightShadows = true;
	bool fillLightShadows = true;
	bool rimLightShadows = true;
	bool directDiffuse = true;
	bool directSpecular = true;
	bool clearcoatSpecular = true;
	float keyLightIntensity = 0.0f;
	float fillLightIntensity = 0.0f;
	float rimLightIntensity = 0.0f;
	float directSpecularScale = 1.0f;
	float pointLightSizeScale = 0.25f;
	uint32_t activeLightSources = 0;

	GpuStats gpu;
	std::vector<std::string> fallbackTextures;
	std::vector<std::string> captures;
};

struct NrayPerformanceReportPaths {
	std::filesystem::path jsonPath;
	std::filesystem::path markdownPath;
	bool jsonWritten = false;
	bool markdownWritten = false;
	std::string error;
};

NrayPerformanceReport makeVulkanPerformanceReport(
	const VulkanComputePreview& preview,
	const VulkanPreviewSettings& settings,
	const GpuStats& stats,
	int width,
	int height,
	bool success,
	const std::string& status,
	const std::vector<std::string>& capturePaths = {},
	double wallTimeSecondsOverride = 0.0
);

std::filesystem::path makeDefaultPerformanceReportPrefix();
std::filesystem::path performanceReportJsonPath(const std::filesystem::path& prefix);
std::filesystem::path performanceReportMarkdownPath(const std::filesystem::path& prefix);
bool writePerformanceReportJson(const std::filesystem::path& path, const NrayPerformanceReport& report, std::string* error = nullptr);
bool writePerformanceReportMarkdown(const std::filesystem::path& path, const NrayPerformanceReport& report, std::string* error = nullptr);
NrayPerformanceReportPaths writePerformanceReportFiles(const std::filesystem::path& prefix, const NrayPerformanceReport& report);
