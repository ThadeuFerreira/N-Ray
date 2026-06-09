#pragma once

#include <bvh.h>
#include <pbr_model.h>
#include <tri.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

inline constexpr uint32_t GLTF_PREVIEW_INVALID_TEXTURE = UINT32_MAX;

enum GltfPreviewAlphaMode : uint32_t {
	GLTF_PREVIEW_ALPHA_OPAQUE = 0,
	GLTF_PREVIEW_ALPHA_MASK = 1,
	GLTF_PREVIEW_ALPHA_BLEND = 2
};

enum GltfPreviewMaterialKind : uint32_t {
	GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC = 0,
	GLTF_PREVIEW_MATERIAL_OPAQUE_METAL = 1,
	GLTF_PREVIEW_MATERIAL_ALPHA_MASK = 2,
	GLTF_PREVIEW_MATERIAL_ALPHA_BLEND_COVERAGE = 3,
	GLTF_PREVIEW_MATERIAL_THIN_TRANSMISSION = 4,
	GLTF_PREVIEW_MATERIAL_VOLUME_TRANSMISSION = 5,
	GLTF_PREVIEW_MATERIAL_UNLIT = 6,
	GLTF_PREVIEW_MATERIAL_CAR_PAINT = 7,
	GLTF_PREVIEW_MATERIAL_RUBBER = 8,
	GLTF_PREVIEW_MATERIAL_EMISSIVE = 9
};

struct GltfPreviewStats {
	uint32_t nodeCount = 0;
	uint32_t meshCount = 0;
	uint32_t primitiveCount = 0;
	uint32_t triangleCount = 0;
	uint32_t materialCount = 0;
	uint32_t textureCount = 0;
	uint32_t imageCount = 0;
};

struct GltfPreviewTexture {
	std::vector<uint8_t> rgba;
	uint32_t width = 1;
	uint32_t height = 1;
	int wrapS = 10497;
	int wrapT = 10497;
	int minFilter = 9729;
	int magFilter = 9729;
	std::string name;
	int sourceImage = -1;
	std::string sourceUri;
	std::string mimeType;
	bool fallback = false;
	std::string fallbackReason;
	bool derived = false;
	std::string derivedFrom;
	std::string channelStats;
};

struct GltfPreviewMaterialMeta {
	glm::vec4 baseColorFactor = glm::vec4(1.0f);
	glm::vec3 emissiveFactor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;
	float roughness = 1.0f;
	float metalness = 1.0f;
	float alphaCutoff = 0.5f;
	float normalScale = 1.0f;
	float occlusionStrength = 1.0f;
	float clearcoatFactor = 0.0f;
	float clearcoatRoughnessFactor = 0.0f;
	float transmission = 0.0f;
	float ior = 1.5f;
	float volumeThickness = 0.0f;
	float attenuationDistance = 0.0f;
	glm::vec3 attenuationColor = glm::vec3(1.0f);
	uint32_t baseColorTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t metallicRoughnessTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t normalTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t occlusionTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t emissiveTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t transmissionTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t thicknessTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t alphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
	bool unlit = false;
	std::string workflow = "metallicRoughness";
	uint32_t specGlossDiffuseTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t specGlossTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	glm::vec3 specGlossSpecularFactor = glm::vec3(1.0f);
	float specGlossGlossinessFactor = 1.0f;
	bool derivedMetallicRoughnessTexture = false;

	uint32_t materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;
	uint32_t normalizedAlphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
	glm::vec4 normalizedBaseColorFactor = glm::vec4(1.0f);
	float normalizedRoughness = 1.0f;
	float normalizedMetalness = 0.0f;
	float normalizedTransmission = 0.0f;
	float normalizedAlphaCoverage = 1.0f;
	float normalizedIor = 1.5f;
	bool inferredTransmission = false;
	bool inferredVolumeThickness = false;
	bool repairedTransmissionTint = false;
	bool repairedMetallicTransmission = false;
	std::string normalizedSemantic;
	std::string name;
};

struct GltfPreviewTriSurface {
	glm::vec2 aUv = glm::vec2(0.0f);
	glm::vec2 bUv = glm::vec2(0.0f);
	glm::vec2 cUv = glm::vec2(0.0f);
	glm::vec4 aTangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	glm::vec4 bTangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	glm::vec4 cTangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
};

struct GltfPreviewScene {
	std::vector<Tri> tris;
	std::vector<TriIntersect> triIsect;
	std::vector<GltfPreviewTriSurface> triSurfaces;
	std::vector<PBRMaterial> materials;
	std::vector<GltfPreviewMaterialMeta> materialMeta;
	std::vector<float> materialOpacity;
	std::vector<float> materialTransmission;
	std::vector<GltfPreviewTexture> textures;
	std::vector<CompactBVH> flatBvh;

	glm::vec3 boundsMin = glm::vec3(0.0f);
	glm::vec3 boundsMax = glm::vec3(0.0f);

	GltfPreviewStats stats;
	std::string sourcePath;
	std::string status;
	bool loaded = false;
};

bool createDefaultGltfPreviewScene(GltfPreviewScene& scene);
bool loadGltfPreviewScene(const std::string& requestedPath, GltfPreviewScene& scene);
