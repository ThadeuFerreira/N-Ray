#include <gltf_scene.h>

#include <renderer.h>

#include <tiny_gltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
constexpr const char* kDefaultGltfPath =
	"assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf";

PBRMaterial makeDefaultMaterial() {
	return PBRMaterial{
		glm::vec3(0.8f),
		glm::vec3(1.0f),
		glm::vec3(0.0f),
		glm::vec3(1.0f),
		glm::vec3(0.0f),
		1.5f,
		0.6f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f
	};
}

PBRMaterial makePreviewMaterial(const glm::vec3& albedo, float roughness = 0.75f) {
	PBRMaterial material = makeDefaultMaterial();
	material.albedo = albedo;
	material.roughness = roughness;
	material.metalness = 0.0f;
	material.emissionIntensity = 0.0f;
	return material;
}

float clamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float maxComponent(const glm::vec3& value) {
	return std::max(value.x, std::max(value.y, value.z));
}

std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

float readTransmissionFactor(const tinygltf::Material& material) {
	auto extIt = material.extensions.find("KHR_materials_transmission");
	if (extIt == material.extensions.end() || !extIt->second.IsObject()) {
		return 0.0f;
	}

	const tinygltf::Value& factor = extIt->second.Get("transmissionFactor");
	if (!factor.IsReal() && !factor.IsInt()) {
		return 0.0f;
	}

	return clamp01(static_cast<float>(factor.GetNumberAsDouble()));
}

float readIor(const tinygltf::Material& material) {
	auto extIt = material.extensions.find("KHR_materials_ior");
	if (extIt == material.extensions.end() || !extIt->second.IsObject()) {
		return 1.5f;
	}

	const tinygltf::Value& ior = extIt->second.Get("ior");
	if (!ior.IsReal() && !ior.IsInt()) {
		return 1.5f;
	}

	return std::max(1.0f, static_cast<float>(ior.GetNumberAsDouble()));
}

float readEmissiveStrength(const tinygltf::Material& material) {
	auto extIt = material.extensions.find("KHR_materials_emissive_strength");
	if (extIt == material.extensions.end() || !extIt->second.IsObject()) {
		return 1.0f;
	}

	const tinygltf::Value& strength = extIt->second.Get("emissiveStrength");
	if (!strength.IsReal() && !strength.IsInt()) {
		return 1.0f;
	}

	return std::max(0.0f, static_cast<float>(strength.GetNumberAsDouble()));
}

uint32_t textureIndexOrInvalid(int textureIndex, size_t textureCount) {
	if (textureIndex < 0 || textureIndex >= static_cast<int>(textureCount)) {
		return GLTF_PREVIEW_INVALID_TEXTURE;
	}
	return static_cast<uint32_t>(textureIndex);
}

uint32_t readTransmissionTexture(const tinygltf::Material& material, size_t textureCount) {
	auto extIt = material.extensions.find("KHR_materials_transmission");
	if (extIt == material.extensions.end() || !extIt->second.IsObject()) {
		return GLTF_PREVIEW_INVALID_TEXTURE;
	}

	const tinygltf::Value& textureInfo = extIt->second.Get("transmissionTexture");
	if (!textureInfo.IsObject()) {
		return GLTF_PREVIEW_INVALID_TEXTURE;
	}

	const tinygltf::Value& index = textureInfo.Get("index");
	if (!index.IsInt()) {
		return GLTF_PREVIEW_INVALID_TEXTURE;
	}

	return textureIndexOrInvalid(index.GetNumberAsInt(), textureCount);
}

struct VolumeExtension {
	float thickness = 0.0f;
	float attenuationDistance = 0.0f;
	glm::vec3 attenuationColor = glm::vec3(1.0f);
	uint32_t thicknessTexture = GLTF_PREVIEW_INVALID_TEXTURE;
};

VolumeExtension readVolumeExtension(const tinygltf::Material& material, size_t textureCount) {
	VolumeExtension result;
	auto extIt = material.extensions.find("KHR_materials_volume");
	if (extIt == material.extensions.end() || !extIt->second.IsObject()) {
		return result;
	}

	const tinygltf::Value& thickness = extIt->second.Get("thicknessFactor");
	if (thickness.IsReal() || thickness.IsInt()) {
		result.thickness = std::max(0.0f, static_cast<float>(thickness.GetNumberAsDouble()));
	}

	const tinygltf::Value& attenuationDistance = extIt->second.Get("attenuationDistance");
	if (attenuationDistance.IsReal() || attenuationDistance.IsInt()) {
		result.attenuationDistance = std::max(0.0f, static_cast<float>(attenuationDistance.GetNumberAsDouble()));
	}

	const tinygltf::Value& attenuationColor = extIt->second.Get("attenuationColor");
	if (attenuationColor.IsArray() && attenuationColor.ArrayLen() >= 3) {
		const tinygltf::Value& r = attenuationColor.Get(0);
		const tinygltf::Value& g = attenuationColor.Get(1);
		const tinygltf::Value& b = attenuationColor.Get(2);
		if (r.IsNumber() && g.IsNumber() && b.IsNumber()) {
			result.attenuationColor = glm::vec3(
				clamp01(static_cast<float>(r.GetNumberAsDouble())),
				clamp01(static_cast<float>(g.GetNumberAsDouble())),
				clamp01(static_cast<float>(b.GetNumberAsDouble()))
			);
		}
	}

	const tinygltf::Value& thicknessTexture = extIt->second.Get("thicknessTexture");
	if (thicknessTexture.IsObject()) {
		const tinygltf::Value& index = thicknessTexture.Get("index");
		if (index.IsInt()) {
			result.thicknessTexture = textureIndexOrInvalid(index.GetNumberAsInt(), textureCount);
		}
	}

	return result;
}

bool hasUnlitExtension(const tinygltf::Material& material) {
	auto extIt = material.extensions.find("KHR_materials_unlit");
	return extIt != material.extensions.end() && extIt->second.IsObject();
}

uint32_t alphaModeValue(const tinygltf::Material& material) {
	if (material.alphaMode == "MASK") {
		return GLTF_PREVIEW_ALPHA_MASK;
	}
	if (material.alphaMode == "BLEND") {
		return GLTF_PREVIEW_ALPHA_BLEND;
	}
	return GLTF_PREVIEW_ALPHA_OPAQUE;
}

const char* alphaModeName(uint32_t alphaMode) {
	switch (alphaMode) {
	case GLTF_PREVIEW_ALPHA_MASK: return "MASK";
	case GLTF_PREVIEW_ALPHA_BLEND: return "BLEND";
	default: return "OPAQUE";
	}
}

const char* materialKindName(uint32_t materialKind) {
	switch (materialKind) {
	case GLTF_PREVIEW_MATERIAL_OPAQUE_METAL: return "OpaqueMetal";
	case GLTF_PREVIEW_MATERIAL_ALPHA_MASK: return "AlphaMask";
	case GLTF_PREVIEW_MATERIAL_ALPHA_BLEND_COVERAGE: return "AlphaBlendCoverage";
	case GLTF_PREVIEW_MATERIAL_THIN_TRANSMISSION: return "ThinTransmission";
	case GLTF_PREVIEW_MATERIAL_VOLUME_TRANSMISSION: return "VolumeTransmission";
	case GLTF_PREVIEW_MATERIAL_UNLIT: return "Unlit";
	case GLTF_PREVIEW_MATERIAL_CAR_PAINT: return "CarPaint";
	case GLTF_PREVIEW_MATERIAL_RUBBER: return "Rubber";
	case GLTF_PREVIEW_MATERIAL_EMISSIVE: return "Emissive";
	default: return "OpaqueDielectric";
	}
}

std::string joinExtensions(const std::vector<std::string>& extensions) {
	std::string result;
	for (size_t i = 0; i < extensions.size(); ++i) {
		if (i > 0) {
			result += ", ";
		}
		result += extensions[i];
	}
	return result;
}

uint8_t floatToByte(float value) {
	return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

GltfPreviewTexture makeFallbackTexture(glm::vec4 color, const std::string& name) {
	GltfPreviewTexture texture;
	texture.name = name;
	texture.width = 1;
	texture.height = 1;
	texture.rgba = {
		floatToByte(color.r),
		floatToByte(color.g),
		floatToByte(color.b),
		floatToByte(color.a)
	};
	return texture;
}

GltfPreviewTexture convertTexture(const tinygltf::Model& model, int textureIndex) {
	if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
		return makeFallbackTexture(glm::vec4(1.0f), "missing texture");
	}

	const tinygltf::Texture& gltfTexture = model.textures[textureIndex];
	if (gltfTexture.source < 0 || gltfTexture.source >= static_cast<int>(model.images.size())) {
		return makeFallbackTexture(glm::vec4(1.0f), gltfTexture.name);
	}

	const tinygltf::Image& image = model.images[gltfTexture.source];
	GltfPreviewTexture texture;
	texture.name = !gltfTexture.name.empty() ? gltfTexture.name : image.name;
	texture.width = static_cast<uint32_t>(std::max(image.width, 1));
	texture.height = static_cast<uint32_t>(std::max(image.height, 1));

	if (gltfTexture.sampler >= 0 && gltfTexture.sampler < static_cast<int>(model.samplers.size())) {
		const tinygltf::Sampler& sampler = model.samplers[gltfTexture.sampler];
		texture.wrapS = sampler.wrapS;
		texture.wrapT = sampler.wrapT;
		texture.minFilter = sampler.minFilter;
		texture.magFilter = sampler.magFilter;
	}

	size_t pixelCount = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
	if (image.as_is || image.image.empty() || image.width <= 0 || image.height <= 0 || image.component <= 0) {
		texture = makeFallbackTexture(glm::vec4(1.0f), texture.name);
		return texture;
	}

	size_t components = static_cast<size_t>(image.component);
	texture.rgba.resize(pixelCount * 4, 255);
	if (image.bits == 8) {
		size_t expectedBytes = pixelCount * components;
		if (image.image.size() < expectedBytes) {
			texture = makeFallbackTexture(glm::vec4(1.0f), texture.name);
			return texture;
		}
		for (size_t i = 0; i < pixelCount; ++i) {
			const unsigned char* pixel = image.image.data() + i * components;
			uint8_t r = pixel[0];
			uint8_t g = components > 1 ? pixel[1] : r;
			uint8_t b = components > 2 ? pixel[2] : r;
			uint8_t a = components > 3 ? pixel[3] : 255;
			texture.rgba[i * 4 + 0] = r;
			texture.rgba[i * 4 + 1] = g;
			texture.rgba[i * 4 + 2] = b;
			texture.rgba[i * 4 + 3] = a;
		}
	}
	else if (image.bits == 16) {
		size_t expectedBytes = pixelCount * components * sizeof(uint16_t);
		if (image.image.size() < expectedBytes) {
			texture = makeFallbackTexture(glm::vec4(1.0f), texture.name);
			return texture;
		}
		for (size_t i = 0; i < pixelCount; ++i) {
			size_t base = i * components;
			auto read16 = [&](size_t channel) {
				uint16_t value = 0;
				std::memcpy(&value, image.image.data() + (base + channel) * sizeof(uint16_t), sizeof(value));
				return static_cast<uint8_t>(value >> 8);
			};
			uint8_t r = read16(0);
			uint8_t g = components > 1 ? read16(1) : r;
			uint8_t b = components > 2 ? read16(2) : r;
			uint8_t a = components > 3 ? read16(3) : 255;
			texture.rgba[i * 4 + 0] = r;
			texture.rgba[i * 4 + 1] = g;
			texture.rgba[i * 4 + 2] = b;
			texture.rgba[i * 4 + 3] = a;
		}
	}
	else {
		texture = makeFallbackTexture(glm::vec4(1.0f), texture.name);
	}

	return texture;
}

glm::vec4 averageBaseColorTexture(const tinygltf::Model& model, int textureIndex) {
	if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
		return glm::vec4(1.0f);
	}

	const tinygltf::Texture& texture = model.textures[textureIndex];
	if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size())) {
		return glm::vec4(1.0f);
	}

	const tinygltf::Image& image = model.images[texture.source];
	if (image.as_is || image.image.empty() || image.width <= 0 || image.height <= 0 || image.component <= 0) {
		return glm::vec4(1.0f);
	}

	glm::vec4 sum(0.0f);
	size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
	size_t components = static_cast<size_t>(image.component);
	if (image.bits == 8) {
		size_t expectedBytes = pixelCount * components;
		if (image.image.size() < expectedBytes) {
			return glm::vec4(1.0f);
		}
		for (size_t i = 0; i < pixelCount; ++i) {
			const unsigned char* pixel = image.image.data() + i * components;
			float r = pixel[0] / 255.0f;
			float g = components > 1 ? pixel[1] / 255.0f : r;
			float b = components > 2 ? pixel[2] / 255.0f : r;
			float a = components > 3 ? pixel[3] / 255.0f : 1.0f;
			sum += glm::vec4(r, g, b, a);
		}
	}
	else if (image.bits == 16) {
		size_t expectedBytes = pixelCount * components * sizeof(uint16_t);
		if (image.image.size() < expectedBytes) {
			return glm::vec4(1.0f);
		}
		for (size_t i = 0; i < pixelCount; ++i) {
			size_t base = i * components;
			auto read16 = [&](size_t channel) {
				uint16_t value = 0;
				std::memcpy(&value, image.image.data() + (base + channel) * sizeof(uint16_t), sizeof(value));
				return value;
			};
			float r = read16(0) / 65535.0f;
			float g = components > 1 ? read16(1) / 65535.0f : r;
			float b = components > 2 ? read16(2) / 65535.0f : r;
			float a = components > 3 ? read16(3) / 65535.0f : 1.0f;
			sum += glm::vec4(r, g, b, a);
		}
	}
	else {
		return glm::vec4(1.0f);
	}

	return sum / static_cast<float>(pixelCount);
}

struct TextureChannelStats {
	bool valid = false;
	float min = 0.0f;
	float mean = 0.0f;
	float max = 0.0f;
};

TextureChannelStats textureChannelStats(const GltfPreviewTexture& texture, size_t channel) {
	TextureChannelStats stats;
	if (channel >= 4 || texture.width == 0 || texture.height == 0) {
		return stats;
	}

	size_t pixelCount = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
	if (texture.rgba.size() < pixelCount * 4) {
		return stats;
	}

	stats.valid = true;
	stats.min = 1.0f;
	stats.max = 0.0f;
	double sum = 0.0;
	for (size_t i = 0; i < pixelCount; ++i) {
		float value = texture.rgba[i * 4 + channel] / 255.0f;
		stats.min = std::min(stats.min, value);
		stats.max = std::max(stats.max, value);
		sum += value;
	}
	stats.mean = static_cast<float>(sum / static_cast<double>(pixelCount));
	return stats;
}

std::string channelStatsString(const TextureChannelStats& stats) {
	if (!stats.valid) {
		return "n/a";
	}

	std::ostringstream out;
	out << std::fixed << std::setprecision(3)
		<< stats.min << "/" << stats.mean << "/" << stats.max;
	return out.str();
}

bool textureAlphaLooksLikeCoverage(uint32_t textureIndex, const std::vector<GltfPreviewTexture>& textures) {
	if (textureIndex == GLTF_PREVIEW_INVALID_TEXTURE || textureIndex >= textures.size()) {
		return false;
	}

	TextureChannelStats alpha = textureChannelStats(textures[textureIndex], 3);
	if (!alpha.valid) {
		return false;
	}

	return alpha.min < 0.95f && (alpha.max - alpha.min) > 0.1f;
}

bool textContainsAny(const std::string& text, std::initializer_list<const char*> needles) {
	for (const char* needle : needles) {
		if (text.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool looksLikeTransmissiveSurface(
	const tinygltf::Material& material,
	const std::string& sourcePath,
	const GltfPreviewMaterialMeta& meta,
	const glm::vec4& averagedBaseColor
) {
	std::string text = lowerAscii(material.name + " " + sourcePath);
	bool nameLooksTransmissive =
		text.find("glass") != std::string::npos ||
		text.find("window") != std::string::npos ||
		text.find("windshield") != std::string::npos ||
		text.find("windscreen") != std::string::npos ||
		text.find("lens") != std::string::npos ||
		text.find("crystal") != std::string::npos ||
		text.find("water") != std::string::npos ||
		text.find("liquid") != std::string::npos;

	if (nameLooksTransmissive) {
		return true;
	}

	bool simpleBlendMaterial =
		meta.alphaMode == GLTF_PREVIEW_ALPHA_BLEND &&
		meta.baseColorTexture == GLTF_PREVIEW_INVALID_TEXTURE &&
		meta.metallicRoughnessTexture == GLTF_PREVIEW_INVALID_TEXTURE &&
		meta.roughness <= 0.15f &&
		averagedBaseColor.a < 0.85f;
	return simpleBlendMaterial;
}

void normalizeOpaqueMaterialSemantics(const tinygltf::Material& material, GltfPreviewMaterialMeta& meta) {
	std::string name = lowerAscii(material.name);
	bool hasMetallicRoughnessTexture = meta.metallicRoughnessTexture != GLTF_PREVIEW_INVALID_TEXTURE;
	float rawMetalness = meta.normalizedMetalness;
	float rawRoughness = meta.normalizedRoughness;

	if (textContainsAny(name, {"tire", "tyre", "rubber", "gasket", "seal"})) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_RUBBER;
		meta.normalizedMetalness = 0.0f;
		meta.normalizedRoughness = std::max(meta.normalizedRoughness, 0.55f);
		meta.normalizedSemantic = "rubber dielectric";
		return;
	}

	if (textContainsAny(name, {"paint", "body", "colour", "color", "lacquer", "clearcoat"})) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_CAR_PAINT;
		if (!hasMetallicRoughnessTexture || meta.normalizedMetalness > 0.35f) {
			meta.normalizedMetalness = std::min(meta.normalizedMetalness, 0.08f);
		}
		meta.normalizedRoughness = std::clamp(meta.normalizedRoughness, 0.03f, 0.65f);
		meta.normalizedSemantic = "paint dielectric";
		return;
	}

	if (textContainsAny(name, {"plastic", "interior", "leather", "cloth", "fabric", "carbon", "cab", "seat", "dash", "trim", "steering"})) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;
		if (!hasMetallicRoughnessTexture || meta.normalizedMetalness > 0.25f) {
			meta.normalizedMetalness = 0.0f;
		}
		meta.normalizedRoughness = std::max(meta.normalizedRoughness, 0.32f);
		meta.normalizedSemantic = "nonmetal dielectric";
		return;
	}

	if (textContainsAny(name, {"light", "lamp", "indicator", "signal"})) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_EMISSIVE;
		meta.normalizedMetalness = 0.0f;
		meta.normalizedRoughness = std::clamp(meta.normalizedRoughness, 0.02f, 0.25f);
		meta.normalizedSemantic = "light surface";
		return;
	}

	if (meta.emissiveTexture != GLTF_PREVIEW_INVALID_TEXTURE || meta.emissiveStrength > 1.0f) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_EMISSIVE;
		meta.normalizedMetalness = 0.0f;
		meta.normalizedRoughness = std::clamp(meta.normalizedRoughness, 0.02f, 0.35f);
		meta.normalizedSemantic = "emissive surface";
		return;
	}

	if (textContainsAny(name, {"metal", "chrome", "steel", "aluminum", "aluminium", "wheel", "rim", "brake", "disc", "rotor", "caliper", "calliper", "exhaust"})) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_METAL;
		meta.normalizedMetalness = std::max(meta.normalizedMetalness, 0.75f);
		meta.normalizedSemantic = "metal surface";
		return;
	}

	if (!hasMetallicRoughnessTexture && std::abs(rawMetalness - 0.5f) < 0.001f) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;
		meta.normalizedMetalness = 0.0f;
		meta.normalizedSemantic = "sparse default dielectric";
		return;
	}

	meta.materialKind = meta.normalizedMetalness > 0.5f
		? GLTF_PREVIEW_MATERIAL_OPAQUE_METAL
		: GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;

	if (meta.materialKind == GLTF_PREVIEW_MATERIAL_OPAQUE_METAL && !hasMetallicRoughnessTexture && rawMetalness > 0.5f && rawRoughness > 0.65f) {
		meta.normalizedMetalness = 0.0f;
		meta.materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;
		meta.normalizedSemantic = "rough sparse material";
	}
}

void normalizeMaterialMeta(
	const tinygltf::Material& material,
	const std::string& sourcePath,
	const std::vector<GltfPreviewTexture>& textures,
	GltfPreviewMaterialMeta& meta,
	const glm::vec4& averagedBaseColor
) {
	float rawAlphaCoverage = meta.alphaMode == GLTF_PREVIEW_ALPHA_OPAQUE ? 1.0f : clamp01(averagedBaseColor.a);
	float transmission = clamp01(meta.transmission);

	if (transmission <= 0.001f && looksLikeTransmissiveSurface(material, sourcePath, meta, averagedBaseColor)) {
		transmission = std::clamp(1.0f - rawAlphaCoverage, 0.35f, 1.0f);
		meta.inferredTransmission = true;
	}

	meta.normalizedBaseColorFactor = meta.baseColorFactor;
	meta.normalizedRoughness = std::clamp(meta.roughness, 0.02f, 1.0f);
	meta.normalizedMetalness = clamp01(meta.metalness);
	meta.normalizedTransmission = transmission;
	meta.normalizedAlphaCoverage = rawAlphaCoverage;
	meta.normalizedIor = std::max(meta.ior, 1.001f);
	meta.normalizedAlphaMode = meta.alphaMode;

	bool transmissive = transmission > 0.001f;
	if (transmissive) {
		meta.materialKind = meta.volumeThickness > 0.001f
			? GLTF_PREVIEW_MATERIAL_VOLUME_TRANSMISSION
			: GLTF_PREVIEW_MATERIAL_THIN_TRANSMISSION;

		if (meta.normalizedMetalness > 0.001f) {
			meta.normalizedMetalness = 0.0f;
			meta.repairedMetallicTransmission = true;
		}

		bool alphaTextureCoverage = textureAlphaLooksLikeCoverage(meta.baseColorTexture, textures);
		meta.normalizedAlphaMode = meta.alphaMode == GLTF_PREVIEW_ALPHA_MASK
			? GLTF_PREVIEW_ALPHA_MASK
			: (alphaTextureCoverage ? GLTF_PREVIEW_ALPHA_BLEND : GLTF_PREVIEW_ALPHA_OPAQUE);

		glm::vec3 averagedRgb(averagedBaseColor);
		if (maxComponent(averagedRgb) < 0.05f) {
			float tintFloor = std::clamp(0.35f + (1.0f - rawAlphaCoverage) * 0.5f, 0.35f, 0.85f);
			meta.normalizedBaseColorFactor.r = std::max(meta.normalizedBaseColorFactor.r, tintFloor);
			meta.normalizedBaseColorFactor.g = std::max(meta.normalizedBaseColorFactor.g, tintFloor);
			meta.normalizedBaseColorFactor.b = std::max(meta.normalizedBaseColorFactor.b, tintFloor);
			meta.repairedTransmissionTint = true;
		}
		return;
	}

	if (meta.unlit) {
		meta.materialKind = GLTF_PREVIEW_MATERIAL_UNLIT;
		return;
	}

	if (meta.alphaMode == GLTF_PREVIEW_ALPHA_MASK) {
		meta.normalizedAlphaMode = GLTF_PREVIEW_ALPHA_MASK;
	}
	else if (meta.alphaMode == GLTF_PREVIEW_ALPHA_BLEND) {
		meta.normalizedAlphaMode = GLTF_PREVIEW_ALPHA_BLEND;
	}
	else {
		meta.normalizedAlphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
	}

	normalizeOpaqueMaterialSemantics(material, meta);
	if (meta.normalizedSemantic.empty()) {
		if (meta.normalizedAlphaMode == GLTF_PREVIEW_ALPHA_MASK) {
			meta.materialKind = GLTF_PREVIEW_MATERIAL_ALPHA_MASK;
		}
		else if (meta.normalizedAlphaMode == GLTF_PREVIEW_ALPHA_BLEND) {
			meta.materialKind = GLTF_PREVIEW_MATERIAL_ALPHA_BLEND_COVERAGE;
		}
	}
}

std::string previewTextureLabel(uint32_t textureIndex, const std::vector<GltfPreviewTexture>& textures) {
	if (textureIndex == GLTF_PREVIEW_INVALID_TEXTURE) {
		return "none";
	}
	if (textureIndex >= textures.size()) {
		std::ostringstream out;
		out << "invalid(" << textureIndex << ")";
		return out.str();
	}

	const GltfPreviewTexture& texture = textures[textureIndex];
	std::ostringstream out;
	out << textureIndex << "(" << texture.width << "x" << texture.height;
	if (!texture.name.empty()) {
		out << ", " << texture.name;
	}
	out << ")";
	return out.str();
}

void logGltfPreviewMaterialImport(
	const tinygltf::Material& material,
	const GltfPreviewMaterialMeta& meta,
	const std::vector<GltfPreviewTexture>& textures,
	size_t materialIndex
) {
	std::cout << "glTF preview material[" << materialIndex << "]";
	if (!material.name.empty()) {
		std::cout << " '" << material.name << "'";
	}

	std::cout
		<< ": baseColorFactor=("
		<< meta.baseColorFactor.r << ", "
		<< meta.baseColorFactor.g << ", "
		<< meta.baseColorFactor.b << ", "
		<< meta.baseColorFactor.a << ")"
		<< " roughnessFactor=" << meta.roughness
		<< " metallicFactor=" << meta.metalness
		<< " alphaMode=" << alphaModeName(meta.alphaMode);

	if (meta.alphaMode == GLTF_PREVIEW_ALPHA_MASK) {
		std::cout << " alphaCutoff=" << meta.alphaCutoff;
	}

	std::cout
		<< " doubleSided=" << (material.doubleSided ? "true" : "false")
		<< " textures baseColor=" << previewTextureLabel(meta.baseColorTexture, textures)
		<< " metallicRoughness=" << previewTextureLabel(meta.metallicRoughnessTexture, textures)
		<< " normal=" << previewTextureLabel(meta.normalTexture, textures)
		<< " occlusion=" << previewTextureLabel(meta.occlusionTexture, textures)
		<< " emissive=" << previewTextureLabel(meta.emissiveTexture, textures)
		<< " transmissionTex=" << previewTextureLabel(meta.transmissionTexture, textures)
		<< " transmissionFactor=" << meta.transmission
		<< " thicknessTex=" << previewTextureLabel(meta.thicknessTexture, textures)
		<< " thicknessFactor=" << meta.volumeThickness
		<< " attenuationColor=("
		<< meta.attenuationColor.r << ", "
		<< meta.attenuationColor.g << ", "
		<< meta.attenuationColor.b << ")"
		<< " attenuationDistance=" << meta.attenuationDistance
		<< " ior=" << meta.ior
		<< " normalized kind=" << materialKindName(meta.materialKind)
		<< " alphaMode=" << alphaModeName(meta.normalizedAlphaMode)
		<< " roughness=" << meta.normalizedRoughness
		<< " metalness=" << meta.normalizedMetalness
		<< " transmission=" << meta.normalizedTransmission
		<< " baseColor=("
		<< meta.normalizedBaseColorFactor.r << ", "
		<< meta.normalizedBaseColorFactor.g << ", "
		<< meta.normalizedBaseColorFactor.b << ", "
		<< meta.normalizedBaseColorFactor.a << ")";

	if (meta.metallicRoughnessTexture != GLTF_PREVIEW_INVALID_TEXTURE &&
		meta.metallicRoughnessTexture < textures.size()) {
		const GltfPreviewTexture& texture = textures[meta.metallicRoughnessTexture];
		std::cout
			<< " mrG(min/mean/max)=" << channelStatsString(textureChannelStats(texture, 1))
			<< " mrB(min/mean/max)=" << channelStatsString(textureChannelStats(texture, 2));
	}

	std::cout << std::endl;

	if (meta.inferredTransmission) {
		std::cout << "  NORMALIZED material[" << materialIndex << "]: inferred thin transmission from material name/alpha/roughness cues\n";
	}
	if (meta.repairedMetallicTransmission) {
		std::cout << "  WARNING material[" << materialIndex << "]: metallicFactor=" << meta.metalness
			<< " with transmissionFactor=" << meta.transmission
			<< " -- normalized to dielectric transmission\n";
	}
	if (meta.repairedTransmissionTint) {
		std::cout << "  NORMALIZED material[" << materialIndex << "]: near-black transmissive tint lifted to keep glass physically visible\n";
	}
	if (!meta.normalizedSemantic.empty()) {
		std::cout << "  NORMALIZED material[" << materialIndex << "]: semantic class " << meta.normalizedSemantic
			<< " mapped to roughness=" << meta.normalizedRoughness
			<< " metalness=" << meta.normalizedMetalness << "\n";
	}
	if (meta.alphaMode == GLTF_PREVIEW_ALPHA_BLEND && meta.transmission < 0.001f) {
		std::string lowerName = material.name;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lowerName.find("glass") != std::string::npos ||
			lowerName.find("window") != std::string::npos ||
			lowerName.find("windshield") != std::string::npos) {
			std::cout << "  NOTE material[" << materialIndex << "]: alphaMode=BLEND with no KHR_materials_transmission"
				<< " -- surface will composite as coverage, not physically transmissive glass\n";
		}
	}
}

PBRMaterial convertMaterial(
	const tinygltf::Model& model,
	const tinygltf::Material& material,
	const std::string& sourcePath,
	const std::vector<GltfPreviewTexture>& textures,
	GltfPreviewMaterialMeta& meta,
	float& opacity,
	float& transmission
) {
	PBRMaterial result = makeDefaultMaterial();

	const auto& pbr = material.pbrMetallicRoughness;
	meta = GltfPreviewMaterialMeta{};
	meta.name = material.name;
	if (pbr.baseColorFactor.size() >= 4) {
		meta.baseColorFactor = {
			static_cast<float>(pbr.baseColorFactor[0]),
			static_cast<float>(pbr.baseColorFactor[1]),
			static_cast<float>(pbr.baseColorFactor[2]),
			static_cast<float>(pbr.baseColorFactor[3])
		};
	}

	meta.baseColorTexture = textureIndexOrInvalid(pbr.baseColorTexture.index, model.textures.size());
	meta.metallicRoughnessTexture = textureIndexOrInvalid(pbr.metallicRoughnessTexture.index, model.textures.size());
	meta.normalTexture = textureIndexOrInvalid(material.normalTexture.index, model.textures.size());
	meta.occlusionTexture = textureIndexOrInvalid(material.occlusionTexture.index, model.textures.size());
	meta.emissiveTexture = textureIndexOrInvalid(material.emissiveTexture.index, model.textures.size());
	meta.normalScale = static_cast<float>(material.normalTexture.scale);
	meta.occlusionStrength = static_cast<float>(material.occlusionTexture.strength);
	meta.roughness = static_cast<float>(pbr.roughnessFactor);
	meta.metalness = static_cast<float>(pbr.metallicFactor);
	meta.alphaMode = alphaModeValue(material);
	meta.alphaCutoff = static_cast<float>(material.alphaCutoff);
	meta.transmission = readTransmissionFactor(material);
	meta.transmissionTexture = readTransmissionTexture(material, model.textures.size());
	meta.ior = readIor(material);
	VolumeExtension volume = readVolumeExtension(material, model.textures.size());
	meta.volumeThickness = volume.thickness;
	meta.attenuationDistance = volume.attenuationDistance;
	meta.attenuationColor = volume.attenuationColor;
	meta.thicknessTexture = volume.thicknessTexture;
	meta.unlit = hasUnlitExtension(material);
	meta.emissiveStrength = readEmissiveStrength(material);
	if (material.emissiveFactor.size() >= 3) {
		meta.emissiveFactor = {
			static_cast<float>(material.emissiveFactor[0]),
			static_cast<float>(material.emissiveFactor[1]),
			static_cast<float>(material.emissiveFactor[2])
		};
	}

	glm::vec4 textureAverage = averageBaseColorTexture(model, pbr.baseColorTexture.index);
	glm::vec4 baseColor = meta.baseColorFactor * textureAverage;
	normalizeMaterialMeta(material, sourcePath, textures, meta, baseColor);

	glm::vec4 normalizedBaseColor = meta.normalizedBaseColorFactor * textureAverage;
	result.albedo = glm::vec3(normalizedBaseColor);

	opacity = meta.normalizedAlphaMode == GLTF_PREVIEW_ALPHA_OPAQUE ? 1.0f : meta.normalizedAlphaCoverage;
	transmission = meta.normalizedTransmission;

	result.roughness = meta.normalizedRoughness;
	result.metalness = meta.normalizedMetalness;
	result.IOR = meta.normalizedIor;
	result.refraction = meta.normalizedTransmission;
	result.absorptionCol = meta.attenuationColor;
	result.absorption = meta.attenuationDistance > 0.0f ? 1.0f / meta.attenuationDistance : 0.0f;
	result.volume = meta.volumeThickness;
	result.volumeCol = meta.attenuationColor;
	result.emissionCol = meta.emissiveFactor;
	result.emissionIntensity = meta.emissiveStrength;

	return result;
}

bool fileExists(const std::filesystem::path& path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

std::string resolveScenePath(const std::string& requestedPath) {
	std::filesystem::path path(requestedPath);
	if (path.is_absolute() && fileExists(path)) {
		return path.lexically_normal().string();
	}

	std::vector<std::filesystem::path> candidates{
		path,
		std::filesystem::path("..") / path
	};

	for (const std::filesystem::path& candidate : candidates) {
		if (fileExists(candidate)) {
			return candidate.lexically_normal().string();
		}
	}

	return requestedPath;
}

bool loadImageDataWithFallback(
	tinygltf::Image* image,
	const int imageIdx,
	std::string* err,
	std::string* warn,
	int reqWidth,
	int reqHeight,
	const unsigned char* bytes,
	int size,
	void* userData
) {
	(void)err;
	(void)userData;
	std::string localErr;
	std::string localWarn;
	bool loaded = tinygltf::LoadImageData(
		image,
		imageIdx,
		&localErr,
		&localWarn,
		reqWidth,
		reqHeight,
		bytes,
		size,
		userData
	);
	if (loaded) {
		if (!localWarn.empty() && warn) {
			*warn += localWarn;
		}
		return true;
	}

	const std::string fallbackName = image && !image->name.empty() ? image->name : std::string();
	if (warn) {
		if (!localErr.empty()) {
			*warn += localErr;
		}
		else if (!localWarn.empty()) {
			*warn += localWarn;
		}
		*warn += "Falling back to 1x1 white texture for image[" + std::to_string(imageIdx) +
			"] name = \"" + fallbackName + "\".\n";
	}

	image->width = 1;
	image->height = 1;
	image->component = 4;
	image->bits = 8;
	image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
	image->as_is = false;
	image->image = { 255, 255, 255, 255 };
	return true;
}

glm::mat4 nodeLocalMatrix(const tinygltf::Node& node) {
	if (node.matrix.size() == 16) {
		glm::mat4 matrix(1.0f);
		for (int col = 0; col < 4; ++col) {
			for (int row = 0; row < 4; ++row) {
				matrix[col][row] = static_cast<float>(node.matrix[col * 4 + row]);
			}
		}
		return matrix;
	}

	glm::vec3 translation(0.0f);
	if (node.translation.size() == 3) {
		translation = {
			static_cast<float>(node.translation[0]),
			static_cast<float>(node.translation[1]),
			static_cast<float>(node.translation[2])
		};
	}

	glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
	if (node.rotation.size() == 4) {
		rotation = glm::quat(
			static_cast<float>(node.rotation[3]),
			static_cast<float>(node.rotation[0]),
			static_cast<float>(node.rotation[1]),
			static_cast<float>(node.rotation[2])
		);
	}

	glm::vec3 scale(1.0f);
	if (node.scale.size() == 3) {
		scale = {
			static_cast<float>(node.scale[0]),
			static_cast<float>(node.scale[1]),
			static_cast<float>(node.scale[2])
		};
	}

	return glm::translate(glm::mat4(1.0f), translation) *
		glm::mat4_cast(rotation) *
		glm::scale(glm::mat4(1.0f), scale);
}

bool accessorBytes(
	const tinygltf::Model& model,
	const tinygltf::Accessor& accessor,
	const unsigned char*& bytes,
	size_t& stride,
	std::string& error
) {
	if (accessor.sparse.isSparse) {
		error = "sparse accessors are not supported by the flat glTF preview importer";
		return false;
	}
	if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
		error = "accessor has no valid bufferView";
		return false;
	}

	const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
	if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) {
		error = "bufferView references an invalid buffer";
		return false;
	}

	const tinygltf::Buffer& buffer = model.buffers[view.buffer];
	int computedStride = accessor.ByteStride(view);
	if (computedStride <= 0) {
		error = "accessor has an invalid byte stride";
		return false;
	}

	int componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
	int componentCount = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
	if (componentSize <= 0 || componentCount <= 0) {
		error = "accessor has an invalid element type";
		return false;
	}

	size_t elementSize = static_cast<size_t>(componentSize) * static_cast<size_t>(componentCount);
	size_t strideBytes = static_cast<size_t>(computedStride);
	if (strideBytes < elementSize) {
		error = "accessor stride is smaller than its element size";
		return false;
	}

	if (view.byteOffset > buffer.data.size() ||
		view.byteLength > buffer.data.size() - view.byteOffset) {
		error = "bufferView range exceeds buffer size";
		return false;
	}
	if (accessor.byteOffset > view.byteLength) {
		error = "accessor offset exceeds bufferView size";
		return false;
	}

	if (accessor.count > 0) {
		size_t maxCountMinusOne = (std::numeric_limits<size_t>::max() - elementSize) / strideBytes;
		if (accessor.count - 1 > maxCountMinusOne) {
			error = "accessor range overflows size_t";
			return false;
		}

		size_t requiredBytes = (accessor.count - 1) * strideBytes + elementSize;
		if (accessor.byteOffset > std::numeric_limits<size_t>::max() - requiredBytes) {
			error = "accessor range overflows size_t";
			return false;
		}

		size_t endInView = accessor.byteOffset + requiredBytes;
		if (endInView > view.byteLength) {
			error = "accessor range exceeds bufferView size";
			return false;
		}
	}

	if (view.byteOffset > std::numeric_limits<size_t>::max() - accessor.byteOffset) {
		error = "accessor absolute offset overflows size_t";
		return false;
	}

	size_t offset = view.byteOffset + accessor.byteOffset;
	bytes = buffer.data.data() + offset;
	stride = strideBytes;
	return true;
}

bool readVec3Accessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::vec3>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing vec3 accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_VEC3 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
		error = "only float VEC3 accessors are supported for positions/normals";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* src = reinterpret_cast<const float*>(bytes + i * stride);
		values[i] = { src[0], src[1], src[2] };
	}

	return true;
}

bool readVec2Accessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::vec2>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing vec2 accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_VEC2 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
		error = "only float VEC2 accessors are supported for texture coordinates";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* src = reinterpret_cast<const float*>(bytes + i * stride);
		values[i] = { src[0], src[1] };
	}

	return true;
}

bool readVec4Accessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::vec4>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing vec4 accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_VEC4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
		error = "only float VEC4 accessors are supported for tangents";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* src = reinterpret_cast<const float*>(bytes + i * stride);
		values[i] = { src[0], src[1], src[2], src[3] };
	}

	return true;
}

bool readJointAccessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::uvec4>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing JOINTS_0 accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_VEC4) {
		error = "JOINTS_0 accessor must be VEC4";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const unsigned char* src = bytes + i * stride;
		switch (accessor.componentType) {
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
			const uint8_t* v = reinterpret_cast<const uint8_t*>(src);
			values[i] = { v[0], v[1], v[2], v[3] };
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
			const uint16_t* v = reinterpret_cast<const uint16_t*>(src);
			values[i] = { v[0], v[1], v[2], v[3] };
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
			const uint32_t* v = reinterpret_cast<const uint32_t*>(src);
			values[i] = { v[0], v[1], v[2], v[3] };
			break;
		}
		default:
			error = "unsupported JOINTS_0 component type";
			return false;
		}
	}

	return true;
}

bool readWeightAccessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::vec4>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing WEIGHTS_0 accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_VEC4) {
		error = "WEIGHTS_0 accessor must be VEC4";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const unsigned char* src = bytes + i * stride;
		switch (accessor.componentType) {
		case TINYGLTF_COMPONENT_TYPE_FLOAT: {
			const float* v = reinterpret_cast<const float*>(src);
			values[i] = { v[0], v[1], v[2], v[3] };
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
			const uint8_t* v = reinterpret_cast<const uint8_t*>(src);
			const float denom = accessor.normalized ? 255.0f : 1.0f;
			values[i] = { v[0] / denom, v[1] / denom, v[2] / denom, v[3] / denom };
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
			const uint16_t* v = reinterpret_cast<const uint16_t*>(src);
			const float denom = accessor.normalized ? 65535.0f : 1.0f;
			values[i] = { v[0] / denom, v[1] / denom, v[2] / denom, v[3] / denom };
			break;
		}
		default:
			error = "unsupported WEIGHTS_0 component type";
			return false;
		}
	}

	return true;
}

bool readMat4Accessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<glm::mat4>& values,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing inverseBindMatrices accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_MAT4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
		error = "inverseBindMatrices accessor must be float MAT4";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	values.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const float* src = reinterpret_cast<const float*>(bytes + i * stride);
		glm::mat4 matrix(1.0f);
		for (int col = 0; col < 4; ++col) {
			for (int row = 0; row < 4; ++row) {
				matrix[col][row] = src[col * 4 + row];
			}
		}
		values[i] = matrix;
	}

	return true;
}

bool readIndexAccessor(
	const tinygltf::Model& model,
	int accessorIndex,
	std::vector<uint32_t>& indices,
	std::string& error
) {
	if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
		error = "missing index accessor";
		return false;
	}

	const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
	if (accessor.type != TINYGLTF_TYPE_SCALAR) {
		error = "index accessor must be SCALAR";
		return false;
	}

	const unsigned char* bytes = nullptr;
	size_t stride = 0;
	if (!accessorBytes(model, accessor, bytes, stride, error)) {
		return false;
	}

	indices.resize(accessor.count);
	for (size_t i = 0; i < accessor.count; ++i) {
		const unsigned char* src = bytes + i * stride;
		switch (accessor.componentType) {
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			indices[i] = *reinterpret_cast<const uint8_t*>(src);
			break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			indices[i] = *reinterpret_cast<const uint16_t*>(src);
			break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			indices[i] = *reinterpret_cast<const uint32_t*>(src);
			break;
		default:
			error = "unsupported index component type";
			return false;
		}
	}

	return true;
}

glm::vec3 safeNormal(const glm::vec3& value, const glm::vec3& fallback) {
	float lenSq = glm::dot(value, value);
	if (!std::isfinite(lenSq) || lenSq <= 0.00000001f) {
		return fallback;
	}
	return glm::normalize(value);
}

bool finiteVec3(const glm::vec3& value) {
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool validTriangleGeometry(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	if (!finiteVec3(a) || !finiteVec3(b) || !finiteVec3(c)) {
		return false;
	}

	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 bc = c - b;
	float abLenSq = glm::dot(ab, ab);
	float acLenSq = glm::dot(ac, ac);
	float bcLenSq = glm::dot(bc, bc);
	float maxEdgeLenSq = std::max(abLenSq, std::max(acLenSq, bcLenSq));
	if (!std::isfinite(maxEdgeLenSq) || maxEdgeLenSq <= 1e-24f) {
		return false;
	}

	glm::vec3 area = glm::cross(ab, ac);
	float areaSq = glm::dot(area, area);
	return std::isfinite(areaSq) && areaSq > maxEdgeLenSq * maxEdgeLenSq * 1e-14f;
}

glm::vec4 makeTangent(
	const glm::vec3& tangent,
	const glm::vec3& normal,
	float handedness,
	const glm::vec3& fallback
) {
	glm::vec3 n = safeNormal(normal, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::vec3 t = tangent - n * glm::dot(n, tangent);
	t = safeNormal(t, fallback);
	return glm::vec4(t, handedness < 0.0f ? -1.0f : 1.0f);
}

glm::vec3 fallbackTangentFromUv(
	const glm::vec3& a,
	const glm::vec3& b,
	const glm::vec3& c,
	const glm::vec2& aUv,
	const glm::vec2& bUv,
	const glm::vec2& cUv,
	const glm::vec3& fallback
) {
	glm::vec3 edge1 = b - a;
	glm::vec3 edge2 = c - a;
	glm::vec2 deltaUv1 = bUv - aUv;
	glm::vec2 deltaUv2 = cUv - aUv;
	float denom = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
	if (std::abs(denom) <= 0.00000001f) {
		return fallback;
	}
	return (edge1 * deltaUv2.y - edge2 * deltaUv1.y) / denom;
}

glm::vec3 orthogonalTangent(const glm::vec3& normal) {
	glm::vec3 n = safeNormal(normal, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	return safeNormal(glm::cross(up, n), glm::vec3(1.0f, 0.0f, 0.0f));
}

void includeBounds(GltfPreviewScene& scene, const Tri& tri, bool& haveBounds) {
	if (!haveBounds) {
		scene.boundsMin = tri.min;
		scene.boundsMax = tri.max;
		haveBounds = true;
		return;
	}

	scene.boundsMin = glm::min(scene.boundsMin, tri.min);
	scene.boundsMax = glm::max(scene.boundsMax, tri.max);
}

float previewBoundsPadding(const GltfPreviewScene& scene) {
	glm::vec3 extent = scene.boundsMax - scene.boundsMin;
	float diagonal = glm::length(extent);
	if (!std::isfinite(diagonal) || diagonal <= 0.0f) {
		return 0.0f;
	}

	return std::clamp(diagonal * 0.00001f, 0.000001f, 0.01f);
}

void padTriangleBoundsForPreview(GltfPreviewScene& scene) {
	float padding = previewBoundsPadding(scene);
	if (padding <= 0.0f) {
		return;
	}

	glm::vec3 pad(padding);
	for (Tri& tri : scene.tris) {
		tri.calculateAABB();
		tri.min -= pad;
		tri.max += pad;
	}
}

uint32_t appendShadowTestMaterial(GltfPreviewScene& scene, const glm::vec3& color, float roughness = 0.75f) {
	uint32_t materialIndex = static_cast<uint32_t>(scene.materials.size());
	scene.materials.push_back(makePreviewMaterial(color, roughness));

	GltfPreviewMaterialMeta meta{};
	meta.baseColorFactor = glm::vec4(color, 1.0f);
	meta.roughness = roughness;
	meta.metalness = 0.0f;
	meta.alphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
	meta.materialKind = GLTF_PREVIEW_MATERIAL_OPAQUE_DIELECTRIC;
	meta.normalizedAlphaMode = GLTF_PREVIEW_ALPHA_OPAQUE;
	meta.normalizedBaseColorFactor = glm::vec4(color, 1.0f);
	meta.normalizedRoughness = roughness;
	meta.normalizedMetalness = 0.0f;
	meta.normalizedTransmission = 0.0f;
	meta.normalizedAlphaCoverage = 1.0f;
	meta.normalizedIor = 1.5f;
	scene.materialMeta.push_back(meta);
	scene.materialOpacity.push_back(1.0f);
	scene.materialTransmission.push_back(0.0f);
	return materialIndex;
}

GltfPreviewTriSurface makeProceduralSurface(
	const glm::vec2& aUv,
	const glm::vec2& bUv,
	const glm::vec2& cUv,
	const glm::vec3& normal
) {
	GltfPreviewTriSurface surface{};
	surface.aUv = aUv;
	surface.bUv = bUv;
	surface.cUv = cUv;
	glm::vec3 tangent = orthogonalTangent(normal);
	surface.aTangent = glm::vec4(tangent, 1.0f);
	surface.bTangent = glm::vec4(tangent, 1.0f);
	surface.cTangent = glm::vec4(tangent, 1.0f);
	return surface;
}

void appendProceduralTriangle(
	GltfPreviewScene& scene,
	bool& haveBounds,
	const glm::vec3& a,
	const glm::vec3& b,
	const glm::vec3& c,
	const glm::vec3& aN,
	const glm::vec3& bN,
	const glm::vec3& cN,
	uint32_t materialIndex,
	const GltfPreviewTriSurface& surface
) {
	uint32_t originalTriIndex = static_cast<uint32_t>(scene.tris.size());
	scene.tris.emplace_back(a, b, c, aN, bN, cN, materialIndex, 0u, true);
	scene.tris.back().idx = originalTriIndex;
	scene.triSurfaces.push_back(surface);
	includeBounds(scene, scene.tris.back(), haveBounds);
	scene.stats.triangleCount++;
}

void appendProceduralQuad(
	GltfPreviewScene& scene,
	bool& haveBounds,
	const glm::vec3& a,
	const glm::vec3& b,
	const glm::vec3& c,
	const glm::vec3& d,
	uint32_t materialIndex
) {
	glm::vec3 normal = safeNormal(glm::cross(b - a, c - a), glm::vec3(0.0f, 0.0f, 1.0f));
	appendProceduralTriangle(
		scene,
		haveBounds,
		a,
		b,
		c,
		normal,
		normal,
		normal,
		materialIndex,
		makeProceduralSurface(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), normal)
	);
	appendProceduralTriangle(
		scene,
		haveBounds,
		a,
		c,
		d,
		normal,
		normal,
		normal,
		materialIndex,
		makeProceduralSurface(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f), normal)
	);
}

void appendProceduralCube(
	GltfPreviewScene& scene,
	bool& haveBounds,
	const glm::vec3& center,
	float sideLength,
	uint32_t materialIndex
) {
	float half = sideLength * 0.5f;
	glm::vec3 mn = center - glm::vec3(half);
	glm::vec3 mx = center + glm::vec3(half);

	glm::vec3 v000(mn.x, mn.y, mn.z);
	glm::vec3 v100(mx.x, mn.y, mn.z);
	glm::vec3 v110(mx.x, mx.y, mn.z);
	glm::vec3 v010(mn.x, mx.y, mn.z);
	glm::vec3 v001(mn.x, mn.y, mx.z);
	glm::vec3 v101(mx.x, mn.y, mx.z);
	glm::vec3 v111(mx.x, mx.y, mx.z);
	glm::vec3 v011(mn.x, mx.y, mx.z);

	appendProceduralQuad(scene, haveBounds, v000, v010, v110, v100, materialIndex);
	appendProceduralQuad(scene, haveBounds, v001, v101, v111, v011, materialIndex);
	appendProceduralQuad(scene, haveBounds, v000, v100, v101, v001, materialIndex);
	appendProceduralQuad(scene, haveBounds, v010, v011, v111, v110, materialIndex);
	appendProceduralQuad(scene, haveBounds, v000, v001, v011, v010, materialIndex);
	appendProceduralQuad(scene, haveBounds, v100, v110, v111, v101, materialIndex);
}

glm::vec3 spherePoint(const glm::vec3& center, float radius, float theta, float phi) {
	float sinTheta = std::sin(theta);
	return center + radius * glm::vec3(
		std::cos(phi) * sinTheta,
		std::sin(phi) * sinTheta,
		std::cos(theta)
	);
}

void appendProceduralSphere(
	GltfPreviewScene& scene,
	bool& haveBounds,
	const glm::vec3& center,
	float radius,
	uint32_t materialIndex
) {
	constexpr int kSegments = 24;
	constexpr int kRings = 12;

	for (int ring = 0; ring < kRings; ++ring) {
		float theta0 = PI * static_cast<float>(ring) / static_cast<float>(kRings);
		float theta1 = PI * static_cast<float>(ring + 1) / static_cast<float>(kRings);
		for (int segment = 0; segment < kSegments; ++segment) {
			float phi0 = 2.0f * PI * static_cast<float>(segment) / static_cast<float>(kSegments);
			float phi1 = 2.0f * PI * static_cast<float>(segment + 1) / static_cast<float>(kSegments);

			glm::vec3 p00 = spherePoint(center, radius, theta0, phi0);
			glm::vec3 p01 = spherePoint(center, radius, theta0, phi1);
			glm::vec3 p10 = spherePoint(center, radius, theta1, phi0);
			glm::vec3 p11 = spherePoint(center, radius, theta1, phi1);

			auto normalAt = [&](const glm::vec3& point) {
				return safeNormal(point - center, glm::vec3(0.0f, 0.0f, 1.0f));
			};

			if (ring > 0) {
				appendProceduralTriangle(
					scene,
					haveBounds,
					p00,
					p10,
					p11,
					normalAt(p00),
					normalAt(p10),
					normalAt(p11),
					materialIndex,
					makeProceduralSurface(
						glm::vec2(static_cast<float>(segment) / kSegments, static_cast<float>(ring) / kRings),
						glm::vec2(static_cast<float>(segment) / kSegments, static_cast<float>(ring + 1) / kRings),
						glm::vec2(static_cast<float>(segment + 1) / kSegments, static_cast<float>(ring + 1) / kRings),
						normalAt(p00)
					)
				);
			}
			if (ring + 1 < kRings) {
				appendProceduralTriangle(
					scene,
					haveBounds,
					p00,
					p11,
					p01,
					normalAt(p00),
					normalAt(p11),
					normalAt(p01),
					materialIndex,
					makeProceduralSurface(
						glm::vec2(static_cast<float>(segment) / kSegments, static_cast<float>(ring) / kRings),
						glm::vec2(static_cast<float>(segment + 1) / kSegments, static_cast<float>(ring + 1) / kRings),
						glm::vec2(static_cast<float>(segment + 1) / kSegments, static_cast<float>(ring) / kRings),
						normalAt(p00)
					)
				);
			}
		}
	}
}

void appendShadowValidationRig(GltfPreviewScene& scene, bool& haveBounds) {
	if (!haveBounds) {
		return;
	}

	glm::vec3 modelMin = scene.boundsMin;
	glm::vec3 modelMax = scene.boundsMax;
	glm::vec3 extent = glm::max(modelMax - modelMin, glm::vec3(0.0001f));
	glm::vec3 center = (modelMin + modelMax) * 0.5f;
	float diagonal = glm::length(extent);
	if (!std::isfinite(diagonal) || diagonal <= 0.0f) {
		diagonal = 1.0f;
	}

	float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
	float propSize = std::clamp(diagonal * 0.18f, maxExtent * 0.18f, maxExtent * 0.45f);
	propSize = std::max(propSize, 0.001f);
	float propRadius = propSize * 0.5f;
	float sideGap = std::max(propSize * 0.8f, maxExtent * 0.08f);
	float groundDrop = std::max(propSize * 0.04f, diagonal * 0.002f);
	float groundZ = modelMin.z - groundDrop;

	float cubeX = modelMin.x - sideGap - propRadius;
	float sphereX = modelMax.x + sideGap + propRadius;
	glm::vec3 cubeCenter(cubeX, center.y, groundZ + propRadius);
	glm::vec3 sphereCenter(sphereX, center.y, groundZ + propRadius);

	float rigMinX = cubeX - propRadius;
	float rigMaxX = sphereX + propRadius;
	float groundCenterX = (rigMinX + rigMaxX) * 0.5f;
	float groundHalfX = (rigMaxX - rigMinX) * 0.5f + propSize * 0.75f;
	float groundHalfY = std::max(extent.y * 0.5f + propSize * 1.25f, propSize * 2.0f);

	uint32_t groundMaterial = appendShadowTestMaterial(scene, glm::vec3(0.58f, 0.60f, 0.56f), 0.9f);
	uint32_t cubeMaterial = appendShadowTestMaterial(scene, glm::vec3(0.86f, 0.32f, 0.20f), 0.65f);
	uint32_t sphereMaterial = appendShadowTestMaterial(scene, glm::vec3(0.22f, 0.42f, 0.86f), 0.55f);

	glm::vec3 g0(groundCenterX - groundHalfX, center.y - groundHalfY, groundZ);
	glm::vec3 g1(groundCenterX + groundHalfX, center.y - groundHalfY, groundZ);
	glm::vec3 g2(groundCenterX + groundHalfX, center.y + groundHalfY, groundZ);
	glm::vec3 g3(groundCenterX - groundHalfX, center.y + groundHalfY, groundZ);
	appendProceduralQuad(scene, haveBounds, g0, g1, g2, g3, groundMaterial);
	appendProceduralCube(scene, haveBounds, cubeCenter, propSize, cubeMaterial);
	appendProceduralSphere(scene, haveBounds, sphereCenter, propRadius, sphereMaterial);
}

glm::mat4 weightedSkinMatrix(
	const std::vector<glm::mat4>& skinMatrices,
	const glm::uvec4& joints,
	const glm::vec4& weights
) {
	glm::mat4 skin(0.0f);
	float totalWeight = 0.0f;
	for (int i = 0; i < 4; ++i) {
		float weight = weights[i];
		if (weight <= 0.0f) {
			continue;
		}

		uint32_t joint = joints[i];
		if (joint >= skinMatrices.size()) {
			continue;
		}

		skin += skinMatrices[joint] * weight;
		totalWeight += weight;
	}

	if (totalWeight <= 0.0f) {
		return glm::mat4(1.0f);
	}

	return skin / totalWeight;
}

bool applyBindPoseSkinning(
	const tinygltf::Model& model,
	const tinygltf::Primitive& primitive,
	const std::vector<glm::mat4>& skinMatrices,
	std::vector<glm::vec3>& positions,
	std::vector<glm::vec3>& normals,
	std::vector<glm::vec4>& tangents,
	bool hasNormals,
	bool hasTangents,
	std::string& error
) {
	auto jointsIt = primitive.attributes.find("JOINTS_0");
	auto weightsIt = primitive.attributes.find("WEIGHTS_0");
	if (jointsIt == primitive.attributes.end() || weightsIt == primitive.attributes.end()) {
		error = "skinned primitive is missing JOINTS_0 or WEIGHTS_0";
		return false;
	}

	std::vector<glm::uvec4> joints;
	std::vector<glm::vec4> weights;
	if (!readJointAccessor(model, jointsIt->second, joints, error) ||
		!readWeightAccessor(model, weightsIt->second, weights, error)) {
		return false;
	}

	if (joints.size() != positions.size() || weights.size() != positions.size()) {
		error = "skin attribute count does not match POSITION count";
		return false;
	}

	for (size_t i = 0; i < positions.size(); ++i) {
		glm::mat4 skin = weightedSkinMatrix(skinMatrices, joints[i], weights[i]);
		positions[i] = glm::vec3(skin * glm::vec4(positions[i], 1.0f));
		if (hasNormals && i < normals.size()) {
			normals[i] = safeNormal(glm::mat3(skin) * normals[i], normals[i]);
		}
		if (hasTangents && i < tangents.size()) {
			glm::vec3 tangent = safeNormal(glm::mat3(skin) * glm::vec3(tangents[i]), glm::vec3(tangents[i]));
			tangents[i] = glm::vec4(tangent, tangents[i].w);
		}
	}

	return true;
}

bool appendPrimitive(
	const tinygltf::Model& model,
	const tinygltf::Primitive& primitive,
	const glm::mat4& world,
	const std::vector<glm::mat4>* skinMatrices,
	uint32_t primitiveIndex,
	uint32_t defaultMaterialIndex,
	GltfPreviewScene& scene,
	bool& haveBounds,
	std::string& error
) {
	if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
		error = "only triangle-list primitives are supported";
		return false;
	}

	auto positionIt = primitive.attributes.find("POSITION");
	if (positionIt == primitive.attributes.end()) {
		error = "primitive is missing POSITION";
		return false;
	}

	std::vector<glm::vec3> positions;
	if (!readVec3Accessor(model, positionIt->second, positions, error)) {
		return false;
	}

	std::vector<glm::vec3> normals;
	auto normalIt = primitive.attributes.find("NORMAL");
	bool hasNormals = normalIt != primitive.attributes.end() &&
		readVec3Accessor(model, normalIt->second, normals, error);
	if (normalIt != primitive.attributes.end() && !hasNormals) {
		return false;
	}

	std::vector<glm::vec2> texcoords;
	auto texcoordIt = primitive.attributes.find("TEXCOORD_0");
	bool hasTexcoords = texcoordIt != primitive.attributes.end() &&
		readVec2Accessor(model, texcoordIt->second, texcoords, error);
	if (texcoordIt != primitive.attributes.end() && !hasTexcoords) {
		return false;
	}

	std::vector<glm::vec4> tangents;
	auto tangentIt = primitive.attributes.find("TANGENT");
	bool hasTangents = tangentIt != primitive.attributes.end() &&
		readVec4Accessor(model, tangentIt->second, tangents, error);
	if (tangentIt != primitive.attributes.end() && !hasTangents) {
		return false;
	}

	std::vector<uint32_t> indices;
	if (primitive.indices >= 0) {
		if (!readIndexAccessor(model, primitive.indices, indices, error)) {
			return false;
		}
	}
	else {
		indices.resize(positions.size());
		for (uint32_t i = 0; i < indices.size(); ++i) {
			indices[i] = i;
		}
	}

	if ((indices.size() % 3) != 0) {
		error = "primitive index count is not divisible by 3";
		return false;
	}

	glm::mat4 effectiveWorld = world;
	if (skinMatrices) {
		if (!applyBindPoseSkinning(model, primitive, *skinMatrices, positions, normals, tangents, hasNormals, hasTangents, error)) {
			return false;
		}
		effectiveWorld = glm::mat4(1.0f);
	}

	glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(effectiveWorld)));
	uint32_t materialIndex = defaultMaterialIndex;
	bool doubleSided = false;
	if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
		materialIndex = static_cast<uint32_t>(primitive.material);
		doubleSided = model.materials[primitive.material].doubleSided;
	}

	// A negative-determinant world matrix (e.g. from a mirrored node with scale [-1,1,1])
	// flips triangle winding from CCW to CW, causing single-sided faces to be silently
	// culled by the Möller-Trumbore det < 0 backface test. Correct by swapping two indices.
	const bool windingFlipped = glm::determinant(glm::mat3(effectiveWorld)) < 0.0f;

	uint32_t appendedTriangles = 0;
	for (size_t i = 0; i < indices.size(); i += 3) {
		uint32_t ia = indices[i + 0];
		uint32_t ib = windingFlipped ? indices[i + 2] : indices[i + 1];
		uint32_t ic = windingFlipped ? indices[i + 1] : indices[i + 2];
		if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) {
			error = "primitive index is out of range";
			return false;
		}

		glm::vec3 a = glm::vec3(effectiveWorld * glm::vec4(positions[ia], 1.0f));
		glm::vec3 b = glm::vec3(effectiveWorld * glm::vec4(positions[ib], 1.0f));
		glm::vec3 c = glm::vec3(effectiveWorld * glm::vec4(positions[ic], 1.0f));
		if (!validTriangleGeometry(a, b, c)) {
			continue;
		}

		glm::vec3 faceNormal = safeNormal(glm::cross(b - a, c - a), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::vec3 aN = faceNormal;
		glm::vec3 bN = faceNormal;
		glm::vec3 cN = faceNormal;

		if (hasNormals && ia < normals.size() && ib < normals.size() && ic < normals.size()) {
			aN = safeNormal(normalMatrix * normals[ia], faceNormal);
			bN = safeNormal(normalMatrix * normals[ib], faceNormal);
			cN = safeNormal(normalMatrix * normals[ic], faceNormal);
			if (windingFlipped) std::swap(bN, cN);
		}

		GltfPreviewTriSurface surface{};
		if (hasTexcoords && ia < texcoords.size() && ib < texcoords.size() && ic < texcoords.size()) {
			surface.aUv = texcoords[ia];
			surface.bUv = texcoords[ib];
			surface.cUv = texcoords[ic];
		}

		if (hasTangents && ia < tangents.size() && ib < tangents.size() && ic < tangents.size()) {
			float handednessScale = windingFlipped ? -1.0f : 1.0f;
			glm::vec4 aT = tangents[ia];
			glm::vec4 bT = tangents[ib];
			glm::vec4 cT = tangents[ic];
			surface.aTangent = makeTangent(normalMatrix * glm::vec3(aT), aN, aT.w * handednessScale, orthogonalTangent(aN));
			surface.bTangent = makeTangent(normalMatrix * glm::vec3(bT), bN, bT.w * handednessScale, orthogonalTangent(bN));
			surface.cTangent = makeTangent(normalMatrix * glm::vec3(cT), cN, cT.w * handednessScale, orthogonalTangent(cN));
		}
		else {
			glm::vec3 fallbackTangent = fallbackTangentFromUv(
				a,
				b,
				c,
				surface.aUv,
				surface.bUv,
				surface.cUv,
				orthogonalTangent(faceNormal)
			);
			surface.aTangent = makeTangent(fallbackTangent, aN, 1.0f, orthogonalTangent(aN));
			surface.bTangent = makeTangent(fallbackTangent, bN, 1.0f, orthogonalTangent(bN));
			surface.cTangent = makeTangent(fallbackTangent, cN, 1.0f, orthogonalTangent(cN));
		}

		uint32_t originalTriIndex = static_cast<uint32_t>(scene.tris.size());
		scene.tris.emplace_back(a, b, c, aN, bN, cN, materialIndex, primitiveIndex, doubleSided);
		scene.tris.back().idx = originalTriIndex;
		scene.triSurfaces.push_back(surface);
		includeBounds(scene, scene.tris.back(), haveBounds);
		++appendedTriangles;
	}

	scene.stats.triangleCount += appendedTriangles;
	return true;
}

bool appendMesh(
	const tinygltf::Model& model,
	const tinygltf::Mesh& mesh,
	const glm::mat4& world,
	const std::vector<glm::mat4>* skinMatrices,
	uint32_t& primitiveIndex,
	uint32_t defaultMaterialIndex,
	GltfPreviewScene& scene,
	bool& haveBounds,
	std::string& error
) {
	for (const tinygltf::Primitive& primitive : mesh.primitives) {
		if (!appendPrimitive(model, primitive, world, skinMatrices, primitiveIndex, defaultMaterialIndex, scene, haveBounds, error)) {
			return false;
		}
		scene.stats.primitiveCount++;
		primitiveIndex++;
	}

	return true;
}

bool computeNodeWorldMatrices(
	const tinygltf::Model& model,
	int nodeIndex,
	const glm::mat4& parent,
	std::vector<glm::mat4>& nodeWorld,
	std::vector<bool>& nodeWorldValid,
	std::string& error
) {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
		error = "scene references an invalid node";
		return false;
	}

	const tinygltf::Node& node = model.nodes[nodeIndex];
	glm::mat4 world = parent * nodeLocalMatrix(node);
	nodeWorld[nodeIndex] = world;
	nodeWorldValid[nodeIndex] = true;

	for (int child : node.children) {
		if (!computeNodeWorldMatrices(model, child, world, nodeWorld, nodeWorldValid, error)) {
			return false;
		}
	}

	return true;
}

bool buildSkinMatrices(
	const tinygltf::Model& model,
	int skinIndex,
	const std::vector<glm::mat4>& nodeWorld,
	const std::vector<bool>& nodeWorldValid,
	std::vector<glm::mat4>& skinMatrices,
	std::string& error
) {
	if (skinIndex < 0 || skinIndex >= static_cast<int>(model.skins.size())) {
		error = "node references an invalid skin";
		return false;
	}

	const tinygltf::Skin& skin = model.skins[skinIndex];
	std::vector<glm::mat4> inverseBindMatrices;
	if (skin.inverseBindMatrices >= 0) {
		if (!readMat4Accessor(model, skin.inverseBindMatrices, inverseBindMatrices, error)) {
			return false;
		}
		if (inverseBindMatrices.size() < skin.joints.size()) {
			error = "inverseBindMatrices count is smaller than skin joint count";
			return false;
		}
	}

	skinMatrices.resize(skin.joints.size(), glm::mat4(1.0f));
	for (size_t i = 0; i < skin.joints.size(); ++i) {
		int jointNode = skin.joints[i];
		if (jointNode < 0 || jointNode >= static_cast<int>(nodeWorld.size())) {
			error = "skin references an invalid joint node";
			return false;
		}
		if (!nodeWorldValid[jointNode]) {
			error = "skin joint is not reachable from the active scene";
			return false;
		}

		glm::mat4 inverseBind = i < inverseBindMatrices.size() ? inverseBindMatrices[i] : glm::mat4(1.0f);
		skinMatrices[i] = nodeWorld[jointNode] * inverseBind;
	}

	return true;
}

bool appendNode(
	const tinygltf::Model& model,
	int nodeIndex,
	const std::vector<glm::mat4>& nodeWorld,
	const std::vector<bool>& nodeWorldValid,
	uint32_t& primitiveIndex,
	uint32_t defaultMaterialIndex,
	GltfPreviewScene& scene,
	bool& haveBounds,
	std::string& error
) {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
		error = "scene references an invalid node";
		return false;
	}

	const tinygltf::Node& node = model.nodes[nodeIndex];
	const glm::mat4& world = nodeWorld[nodeIndex];

	std::vector<glm::mat4> skinMatrices;
	const std::vector<glm::mat4>* activeSkinMatrices = nullptr;
	if (node.skin >= 0) {
		if (!buildSkinMatrices(model, node.skin, nodeWorld, nodeWorldValid, skinMatrices, error)) {
			return false;
		}
		activeSkinMatrices = &skinMatrices;
	}

	if (node.mesh >= 0) {
		if (node.mesh >= static_cast<int>(model.meshes.size())) {
			error = "node references an invalid mesh";
			return false;
		}
		if (!appendMesh(model, model.meshes[node.mesh], world, activeSkinMatrices, primitiveIndex, defaultMaterialIndex, scene, haveBounds, error)) {
			return false;
		}
	}

	for (int child : node.children) {
		if (!appendNode(model, child, nodeWorld, nodeWorldValid, primitiveIndex, defaultMaterialIndex, scene, haveBounds, error)) {
			return false;
		}
	}

	return true;
}

void buildSceneAcceleration(GltfPreviewScene& scene) {
	scene.flatBvh.clear();
	scene.triIsect.clear();

	if (scene.tris.empty()) {
		return;
	}

	padTriangleBoundsForPreview(scene);

	std::vector<BVH> buildBvh;
	buildBvh.reserve(scene.tris.size() * 2);
	buildBvh.emplace_back();
	buildBvh[0] = BVH(0, static_cast<uint32_t>(scene.tris.size() - 1), scene.tris, buildBvh);

	PathTracer tracer;
	tracer.flattenBVH(0, buildBvh, scene.flatBvh);

	// flattenBVH reorders scene.tris; tri.idx still holds the pre-flatten index, so
	// reorder the parallel surfaces into a fresh buffer to match. Out-of-range indices
	// (e.g. a missing/short triSurfaces array) keep the default-constructed surface.
	std::vector<GltfPreviewTriSurface> reorderedSurfaces(scene.tris.size());
	for (size_t i = 0; i < scene.tris.size(); ++i) {
		uint32_t originalTriIndex = scene.tris[i].idx;
		if (originalTriIndex < scene.triSurfaces.size()) {
			reorderedSurfaces[i] = scene.triSurfaces[originalTriIndex];
		}
	}
	scene.triSurfaces = std::move(reorderedSurfaces);

	scene.triIsect.resize(scene.tris.size());
	for (size_t i = 0; i < scene.tris.size(); ++i) {
		Tri& tri = scene.tris[i];
		tri.idx = static_cast<uint32_t>(i);
		scene.triIsect[i] = { tri.a, tri.eA, tri.eB, tri.idx, tri.doubleSided ? 1u : 0u };
	}
}

void applyPreviewVolumeFallbacks(GltfPreviewScene& scene) {
	glm::vec3 extent = scene.boundsMax - scene.boundsMin;
	float sceneScale = glm::length(extent);
	if (!std::isfinite(sceneScale) || sceneScale <= 0.0001f) {
		sceneScale = 1.0f;
	}

	float inferredThickness = std::clamp(sceneScale * 0.03f, 0.002f, 0.25f);
	for (size_t i = 0; i < scene.materialMeta.size() && i < scene.materials.size(); ++i) {
		GltfPreviewMaterialMeta& meta = scene.materialMeta[i];
		if (meta.materialKind != GLTF_PREVIEW_MATERIAL_THIN_TRANSMISSION ||
			meta.normalizedTransmission <= 0.001f ||
			meta.volumeThickness > 0.001f) {
			continue;
		}

		meta.volumeThickness = inferredThickness;
		meta.materialKind = GLTF_PREVIEW_MATERIAL_VOLUME_TRANSMISSION;
		meta.inferredVolumeThickness = true;
		if (meta.attenuationDistance <= 0.001f) {
			meta.attenuationDistance = std::max(inferredThickness * 8.0f, 0.01f);
		}
		if (!meta.normalizedSemantic.empty()) {
			meta.normalizedSemantic += ", ";
		}
		meta.normalizedSemantic += "preview volume glass";

		PBRMaterial& material = scene.materials[i];
		material.volume = meta.volumeThickness;
		material.absorptionCol = meta.attenuationColor;
		material.absorption = meta.attenuationDistance > 0.0f ? 1.0f / meta.attenuationDistance : 0.0f;
		material.volumeCol = meta.attenuationColor;

		std::cout << "  NORMALIZED material[" << i << "]: inferred preview volume thickness="
			<< meta.volumeThickness << " attenuationDistance=" << meta.attenuationDistance
			<< " for transmission without KHR_materials_volume\n";
	}
}
}

std::string defaultGltfPreviewPath() {
	return kDefaultGltfPath;
}

bool loadGltfPreviewScene(const std::string& requestedPath, GltfPreviewScene& scene) {
	scene = GltfPreviewScene{};
	scene.sourcePath = resolveScenePath(requestedPath);

	tinygltf::TinyGLTF loader;
	loader.SetImageLoader(loadImageDataWithFallback, nullptr);

	tinygltf::Model model;
	std::string error;
	std::string warning;

	std::filesystem::path source(scene.sourcePath);
	std::string ext = source.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	bool loaded = false;
	if (ext == ".glb") {
		loaded = loader.LoadBinaryFromFile(&model, &error, &warning, scene.sourcePath);
	}
	else {
		loaded = loader.LoadASCIIFromFile(&model, &error, &warning, scene.sourcePath);
	}

	if (!loaded) {
		scene.status = "Failed to load glTF: " + error;
		return false;
	}

	std::string requiredExtensionWarning;
	if (!model.extensionsRequired.empty()) {
		requiredExtensionWarning = "preview ignored required glTF extensions: " + joinExtensions(model.extensionsRequired);
	}

	scene.stats.nodeCount = static_cast<uint32_t>(model.nodes.size());
	scene.stats.meshCount = static_cast<uint32_t>(model.meshes.size());
	scene.stats.materialCount = static_cast<uint32_t>(model.materials.size());
	scene.stats.textureCount = static_cast<uint32_t>(model.textures.size());
	scene.stats.imageCount = static_cast<uint32_t>(model.images.size());

	scene.materials.reserve(model.materials.size() + 1);
	scene.materialMeta.reserve(model.materials.size() + 1);
	scene.materialOpacity.reserve(model.materials.size() + 1);
	scene.materialTransmission.reserve(model.materials.size() + 1);
	scene.textures.reserve(model.textures.size());
	for (int textureIndex = 0; textureIndex < static_cast<int>(model.textures.size()); ++textureIndex) {
		scene.textures.push_back(convertTexture(model, textureIndex));
	}
	for (size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
		const tinygltf::Material& material = model.materials[materialIndex];
		GltfPreviewMaterialMeta meta;
		float opacity = 1.0f;
		float transmission = 0.0f;
		scene.materials.push_back(convertMaterial(model, material, scene.sourcePath, scene.textures, meta, opacity, transmission));
		scene.materialMeta.push_back(meta);
		scene.materialOpacity.push_back(opacity);
		scene.materialTransmission.push_back(transmission);
		logGltfPreviewMaterialImport(material, meta, scene.textures, materialIndex);
	}
	uint32_t defaultMaterialIndex = static_cast<uint32_t>(scene.materials.size());
	scene.materials.push_back(makeDefaultMaterial());
	scene.materialMeta.push_back(GltfPreviewMaterialMeta{});
	scene.materialOpacity.push_back(1.0f);
	scene.materialTransmission.push_back(0.0f);

	int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
	if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size())) {
		scene.status = "glTF has no valid default scene";
		return false;
	}

	// glTF scenes are Y-up. N-Ray's camera/navigation convention is Z-up, so
	// convert imported preview geometry before building bounds and BVH data.
	const glm::mat4 gltfToNray = glm::rotate(
		glm::mat4(1.0f),
		glm::radians(90.0f),
		glm::vec3(1.0f, 0.0f, 0.0f)
	);

	std::vector<glm::mat4> nodeWorld(model.nodes.size(), glm::mat4(1.0f));
	std::vector<bool> nodeWorldValid(model.nodes.size(), false);
	for (int nodeIndex : model.scenes[sceneIndex].nodes) {
		if (!computeNodeWorldMatrices(model, nodeIndex, gltfToNray, nodeWorld, nodeWorldValid, error)) {
			scene.status = "Failed to import glTF geometry: " + error;
			return false;
		}
	}

	bool haveBounds = false;
	uint32_t primitiveIndex = 0;
	for (int nodeIndex : model.scenes[sceneIndex].nodes) {
		if (!appendNode(model, nodeIndex, nodeWorld, nodeWorldValid, primitiveIndex, defaultMaterialIndex, scene, haveBounds, error)) {
			scene.status = "Failed to import glTF geometry: " + error;
			return false;
		}
	}

	if (scene.tris.empty()) {
		scene.status = "glTF scene has no renderable triangles";
		return false;
	}

	applyPreviewVolumeFallbacks(scene);
	appendShadowValidationRig(scene, haveBounds);
	buildSceneAcceleration(scene);
	scene.loaded = true;

	std::ostringstream out;
	out << "glTF model ready: " << scene.stats.primitiveCount << " primitives, "
		<< scene.stats.triangleCount << " triangles, "
		<< scene.stats.materialCount << " materials, shadow validation rig";
	if (!warning.empty()) {
		out << " (warning: " << warning << ")";
	}
	if (!requiredExtensionWarning.empty()) {
		out << " (" << requiredExtensionWarning << ")";
	}
	scene.status = out.str();
	return true;
}
