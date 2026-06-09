#pragma once

#include <glm/glm.hpp>
#include <raylib.h>
#include <tri.h>
#include <render_types.h>
#include <pbr_model.h>

// Allowed range for progressive sample accumulation. Single source of truth
// shared by the CPU path tracer, the Vulkan preview driver, and the UI slider.
inline constexpr int kMinSamples = 1;
inline constexpr int kMaxSamples = 1000;

struct Data {
	std::vector<Tri> tris;
	std::vector<TriIntersect> triIsect;
	std::vector<Tri> emTris;
	std::vector<PBRMaterial> materials;
	std::vector<RenderPixel> frameBuffer;
	std::vector<glm::vec3> accumBuffer;
	std::vector<PTModel> models;
};
 
struct Params { 
	glm::ivec2 screenSize = { 1920, 1080 };

	float dt = 0.0f;

	int res = 512;

	int maxBounces = 5;
	int maxSamples = 10;
	int raysPerPixel = 1;
	int currentSample = 0;

	// Russian roulette: probabilistically terminate low-energy paths after a few
	// bounces. Unbiased (surviving paths are reweighted by 1/p), so the converged
	// image is unchanged while average path length drops.
	bool russianRoulette = true;
	int rrMinBounces = 3;
	bool enableSky = true;
	bool enableEnvironment = true;
	float skyIntensity = 0.75f;
	float blur = 1.0f;
	float exposure = 1.0f;
	float contrast = 0.8f;

	bool enableSun = false;
	glm::vec3 sunDir = { 0.0f, 0.0f, 1.0f };
	glm::vec3 sunColor = { 1.0f, 1.0f, 0.95f };
	float sunIntensity = 100.0f;
	float sunAngle = 7.53f;
	bool enableThreePointLighting = true;
	float keyLightIntensity = 16.0f;
	float fillLightIntensity = 4.0f;
	float rimLightIntensity = 12.0f;
	size_t emissiveAmount = 0;

	bool renderStatsActive = false;
	bool renderStatsComplete = false;
	int renderStatsSample = 0;
	unsigned long long renderStatsTotalRays = 0;
	double renderStatsElapsedSec = 0.0;
	double renderStatsRaysPerSec = 0.0;
	double renderStatsMsPerSample = 0.0;
	unsigned long long renderStatsPublishedFrames = 0;
	double renderStatsPublishMs = 0.0;
	double renderStatsSamplesPerSec = 0.0;
	int renderWorkerThreads = 0;
	int renderPublishHz = 30;

	bool shouldSample = true;
	bool enableSampling = true;
	bool renderInvalidated = false;
	bool displayInvalidated = false;

	bool isMouseHoveringUI = false;
	bool enableDebugRay = false;
	bool debugMaterialColors = false;
	bool enableSelection = true;
	bool useVulkanPreview = true;
	bool render = false;
};
