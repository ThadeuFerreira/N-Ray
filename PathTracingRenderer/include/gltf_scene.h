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
	float transmission = 0.0f;
	uint32_t baseColorTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t metallicRoughnessTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t normalTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t occlusionTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t emissiveTexture = GLTF_PREVIEW_INVALID_TEXTURE;
	uint32_t alphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
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

std::string defaultGltfPreviewPath();
bool loadGltfPreviewScene(const std::string& requestedPath, GltfPreviewScene& scene);
