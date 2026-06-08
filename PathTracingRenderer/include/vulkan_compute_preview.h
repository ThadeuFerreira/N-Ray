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

enum class VulkanPreviewShadowMode : uint32_t {
	None = 0,
	RayTraced = 1,
	ShadowMap = 2
};

enum class VulkanDenoiserMode : uint32_t {
	Off = 0,
	SpatialAtrous = 1,
	SvgfLite = 2
};

enum class VulkanDenoiserDebugView : uint32_t {
	Final = 0,
	RawAccumulation = 1,
	DenoisedPreview = 2,
	Normal = 3,
	Albedo = 4,
	Depth = 5,
	MaterialId = 6,
	InstanceId = 7
};

enum class VulkanPreviewOpticalMode : uint32_t {
	Coverage = 0,
	ThinTransmission = 1,
	VolumeTransmission = 2
};

struct VulkanDenoiserSettings {
	VulkanDenoiserMode mode = VulkanDenoiserMode::SpatialAtrous;
	VulkanDenoiserDebugView debugView = VulkanDenoiserDebugView::Final;
	uint32_t maxAtrousPasses = 4;
	uint32_t fadeOutStartSample = 16;
	uint32_t fadeOutEndSample = 128;
	float depthSigma = 60.0f;
	float normalSigma = 64.0f;
	float lumaSigma = 4.0f;
	bool fireflyClamp = true;
	bool verboseLogging = false;
};

struct VulkanDenoiserStats {
	bool enabled = false;
	bool active = false;
	VulkanDenoiserMode mode = VulkanDenoiserMode::Off;
	VulkanDenoiserDebugView debugView = VulkanDenoiserDebugView::Final;
	float strength = 0.0f;
	uint32_t passCount = 0;
	uint32_t resetCount = 0;
	uint64_t targetResourceBytes = 0;
	double prepareMs = 0.0;
	double atrousMs = 0.0;
	double compositeMs = 0.0;
	double totalMs = 0.0;
	std::string skipReason;
	std::string status;
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
	VulkanPreviewShadowMode shadowMode = VulkanPreviewShadowMode::RayTraced;
	VulkanDenoiserSettings denoiser;
	bool resetAccumulation = false;
	bool postprocessOnly = false;
};

// Live, per-material overrides for the glTF model preview. These values are the
// imported PBR *factors* (baseColorFactor / roughnessFactor / metallicFactor),
// which the shader multiplies by any bound textures. So dropping metalness to 0
// turns a fully-metallic import back into a diffuse surface, and editing
// baseColor re-tints an otherwise grayscale albedo. The texture flags let the UI
// note that a slider multiplies an underlying texture.
struct VulkanPreviewMaterialState {
	// Editable factor overrides — each multiplied by its texture sample in the shader
	glm::vec3 baseColor       = glm::vec3(1.0f);
	float     alpha           = 1.0f;
	float     roughness       = 1.0f;
	float     metalness       = 1.0f;
	glm::vec3 emissiveFactor  = glm::vec3(0.0f);
	float     emissiveIntensity = 1.0f;
	float     transmission    = 0.0f;
	float     ior             = 1.5f;
	VulkanPreviewOpticalMode opticalMode = VulkanPreviewOpticalMode::Coverage;
	float     volumeThickness = 0.0f;
	glm::vec3 attenuationColor = glm::vec3(1.0f);
	float     attenuationDistance = 0.0f;
	float     normalScale     = 1.0f;

	// Read-only texture-presence flags — used by the UI to show "(× tex)" hints
	bool hasBaseColorTexture          = false;
	bool hasMetallicRoughnessTexture  = false;
	bool hasNormalTexture             = false;
	bool hasEmissiveTexture           = false;
	bool hasOcclusionTexture          = false;
	bool hasTransmissionTexture       = false;
	bool hasThicknessTexture          = false;

	// Read-only display info
	std::string name;           // original glTF material name (may be empty)
	std::string semanticLabel;  // normalizedSemantic ("car_paint", "metal surface", …)
	uint32_t    materialKind    = 0u;  // GLTF_PREVIEW_MATERIAL_* constant
	bool        isTransmission  = false;  // thin or volume transmission material
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
	VulkanDenoiserStats denoiser;
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
	bool requestRenderDocCapture();
	bool setCaptureTemplate(const std::string& pathTemplate);
	void setPersistSettings(bool persist);

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
	int importModelFromFolder(const std::string& folderPath, bool persist = true);
	bool modelBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const;

	// Live material overrides for the active glTF model preview. materialCount()
	// is 0 unless a model's buffers are loaded. setMaterialState re-uploads the
	// material SSBO and restarts accumulation; resetMaterialStates restores the
	// as-imported factors.
	int materialCount() const;
	const char* materialName(int index) const;
	bool materialState(int index, VulkanPreviewMaterialState& out) const;
	bool setMaterialState(int index, const VulkanPreviewMaterialState& state);
	void resetMaterialStates();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
