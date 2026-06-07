#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <render_types.h>

struct VulkanPreviewCamera {
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 forward = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
	float verticalScale = 1.0f;
	float aspect = 1.0f;
};

// Per-dispatch render/sky settings for the progressive model path tracer. The
// driver fills this from the global render params each frame; resetAccumulation
// restarts the sample accumulation (set on any camera/model/setting change).
struct VulkanPreviewSettings {
	int maxSamples = 1;
	int maxBounces = 5;
	int rrMinBounces = 3;
	bool russianRoulette = true;
	float exposure = 1.0f;
	float contrast = 0.8f;
	float skyIntensity = 0.75f;
	bool enableSky = true;
	bool enableSun = false;
	glm::vec3 sunDir = glm::vec3(0.0f, 0.0f, 1.0f);
	float sunAngle = 7.53f;
	glm::vec3 sunColor = glm::vec3(1.0f, 1.0f, 0.95f);
	float sunIntensity = 100.0f;
	bool resetAccumulation = false;
};

// Live, per-material overrides for the glTF model preview. These values are the
// imported PBR *factors* (baseColorFactor / roughnessFactor / metallicFactor),
// which the shader multiplies by any bound textures. So dropping metalness to 0
// turns a fully-metallic import back into a diffuse surface, and editing
// baseColor re-tints an otherwise grayscale albedo. The texture flags let the UI
// note that a slider multiplies an underlying texture.
struct VulkanPreviewMaterialState {
	glm::vec3 baseColor = glm::vec3(1.0f);
	float roughness = 1.0f;
	float metalness = 1.0f;
	bool hasBaseColorTexture = false;
	bool hasMetallicRoughnessTexture = false;
};

struct GpuStats {
	double gpuDispatchMs = 0.0;
	double primaryRaysPerSec = 0.0;
	uint32_t frameCount = 0;
	uint64_t primaryRaysTraced = 0;
	uint64_t localHeapBytes = 0;
	uint64_t localHeapUsed = 0;
	uint64_t pixelBufferBytes = 0;
	bool memBudgetAvailable = false;
	bool timestampAvailable = false;
	uint32_t samplesAccumulated = 0;
	uint32_t maxSamples = 1;
};

class VulkanComputePreview {
public:
	VulkanComputePreview();
	~VulkanComputePreview();

	VulkanComputePreview(const VulkanComputePreview&) = delete;
	VulkanComputePreview& operator=(const VulkanComputePreview&) = delete;

	bool initialize(int width, int height);
	bool resize(int width, int height);
	bool render(float timeSeconds, const VulkanPreviewCamera& camera, const VulkanPreviewSettings& settings, std::vector<RenderPixel>& pixels);
	void shutdown();

	// Progressive accumulation progress: completed sample count and whether it has
	// reached the configured maxSamples.
	uint32_t samplesAccumulated() const;
	bool converged() const;

	bool isAvailable() const;
	int width() const;
	int height() const;
	const std::string& statusMessage() const;
	GpuStats gpuStats() const;

	bool setShader(int index);
	int shaderIndex() const;
	static int shaderCount();
	static const char* shaderName(int index);
	static bool isModelPreviewIndex(int index);
	// Index of the first shader that renders an imported glTF model, or -1 if the
	// build has no model-preview shader. Used to auto-activate model preview on import.
	static int modelPreviewShaderIndex();
	bool setModel(int index);
	int modelIndex() const;
	static int modelCount();
	static const char* modelName(int index);
	int importModelFromFolder(const std::string& folderPath);
	bool modelBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const;

	// Live material overrides for the active glTF model preview. materialCount()
	// is 0 unless a model's buffers are loaded. setMaterialState re-uploads the
	// material SSBO and restarts accumulation; resetMaterialStates restores the
	// as-imported factors.
	int materialCount() const;
	bool materialState(int index, VulkanPreviewMaterialState& out) const;
	bool setMaterialState(int index, const VulkanPreviewMaterialState& state);
	void resetMaterialStates();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
