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

float clamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float maxComponent(const glm::vec3& value) {
	return std::max(value.x, std::max(value.y, value.z));
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

uint32_t alphaModeValue(const tinygltf::Material& material) {
	if (material.alphaMode == "MASK") {
		return GLTF_PREVIEW_ALPHA_MASK;
	}
	if (material.alphaMode == "BLEND") {
		return GLTF_PREVIEW_ALPHA_BLEND;
	}
	return GLTF_PREVIEW_ALPHA_OPAQUE;
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

	glm::dvec4 sum(0.0);
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
			sum += glm::dvec4(r, g, b, a);
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
			sum += glm::dvec4(r, g, b, a);
		}
	}
	else {
		return glm::vec4(1.0f);
	}

	glm::dvec4 average = sum / static_cast<double>(pixelCount);
	return glm::vec4(average);
}

PBRMaterial convertMaterial(
	const tinygltf::Model& model,
	const tinygltf::Material& material,
	GltfPreviewMaterialMeta& meta,
	float& opacity,
	float& transmission
) {
	PBRMaterial result = makeDefaultMaterial();

	const auto& pbr = material.pbrMetallicRoughness;
	meta = GltfPreviewMaterialMeta{};
	if (pbr.baseColorFactor.size() >= 4) {
		meta.baseColorFactor = {
			static_cast<float>(pbr.baseColorFactor[0]),
			static_cast<float>(pbr.baseColorFactor[1]),
			static_cast<float>(pbr.baseColorFactor[2]),
			static_cast<float>(pbr.baseColorFactor[3])
		};
	}

	meta.baseColorTexture = textureIndexOrInvalid(pbr.baseColorTexture.index, model.textures.size());
	meta.normalTexture = textureIndexOrInvalid(material.normalTexture.index, model.textures.size());
	meta.emissiveTexture = textureIndexOrInvalid(material.emissiveTexture.index, model.textures.size());
	meta.normalScale = static_cast<float>(material.normalTexture.scale);
	meta.roughness = static_cast<float>(pbr.roughnessFactor);
	meta.metalness = static_cast<float>(pbr.metallicFactor);
	meta.alphaMode = alphaModeValue(material);
	meta.alphaCutoff = static_cast<float>(material.alphaCutoff);
	meta.transmission = readTransmissionFactor(material);
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
	result.albedo = glm::vec3(baseColor);

	opacity = meta.alphaMode == GLTF_PREVIEW_ALPHA_OPAQUE ? 1.0f : clamp01(baseColor.a);
	transmission = meta.transmission;
	if (transmission > 0.0f) {
		opacity = std::min(opacity, 1.0f - transmission * 0.75f);
	}

	// Some transmission materials encode colorless glass as black RGB plus alpha.
	// The flat one-hit Vulkan preview cannot trace through glass yet, so give
	// near-black transparent panes a visible tint instead of drawing them opaque.
	if ((opacity < 0.999f || transmission > 0.0f) && maxComponent(result.albedo) < 0.03f) {
		result.albedo = glm::vec3(0.34f, 0.46f, 0.56f);
	}

	result.roughness = meta.roughness;
	result.metalness = meta.metalness;
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
	bool hasNormals,
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
		if (!applyBindPoseSkinning(model, primitive, *skinMatrices, positions, normals, hasNormals, error)) {
			return false;
		}
		effectiveWorld = glm::mat4(1.0f);
	}

	glm::mat3 normalMatrix = skinMatrices ? glm::mat3(1.0f) : glm::transpose(glm::inverse(glm::mat3(world)));
	uint32_t materialIndex = defaultMaterialIndex;
	bool doubleSided = false;
	if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
		materialIndex = static_cast<uint32_t>(primitive.material);
		doubleSided = model.materials[primitive.material].doubleSided;
	}

	for (size_t i = 0; i < indices.size(); i += 3) {
		uint32_t ia = indices[i + 0];
		uint32_t ib = indices[i + 1];
		uint32_t ic = indices[i + 2];
		if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) {
			error = "primitive index is out of range";
			return false;
		}

		glm::vec3 a = glm::vec3(effectiveWorld * glm::vec4(positions[ia], 1.0f));
		glm::vec3 b = glm::vec3(effectiveWorld * glm::vec4(positions[ib], 1.0f));
		glm::vec3 c = glm::vec3(effectiveWorld * glm::vec4(positions[ic], 1.0f));

		glm::vec3 faceNormal = safeNormal(glm::cross(b - a, c - a), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::vec3 aN = faceNormal;
		glm::vec3 bN = faceNormal;
		glm::vec3 cN = faceNormal;

		if (hasNormals && ia < normals.size() && ib < normals.size() && ic < normals.size()) {
			aN = safeNormal(normalMatrix * normals[ia], faceNormal);
			bN = safeNormal(normalMatrix * normals[ib], faceNormal);
			cN = safeNormal(normalMatrix * normals[ic], faceNormal);
		}

		scene.tris.emplace_back(a, b, c, aN, bN, cN, materialIndex, primitiveIndex, doubleSided);
		includeBounds(scene, scene.tris.back(), haveBounds);
	}

	scene.stats.triangleCount += static_cast<uint32_t>(indices.size() / 3);
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

	std::vector<BVH> buildBvh;
	buildBvh.reserve(scene.tris.size() * 2);
	buildBvh.emplace_back();
	buildBvh[0] = BVH(0, static_cast<uint32_t>(scene.tris.size() - 1), scene.tris, buildBvh);

	PathTracer tracer;
	tracer.flattenBVH(0, buildBvh, scene.flatBvh);

	scene.triIsect.resize(scene.tris.size());
	for (size_t i = 0; i < scene.tris.size(); ++i) {
		Tri& tri = scene.tris[i];
		tri.idx = static_cast<uint32_t>(i);
		scene.triIsect[i] = { tri.a, tri.eA, tri.eB, tri.idx, tri.doubleSided ? 1u : 0u };
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

	if (!model.extensionsRequired.empty()) {
		scene.status = "Unsupported required glTF extension: " + model.extensionsRequired[0];
		for (size_t i = 1; i < model.extensionsRequired.size(); ++i) {
			scene.status += ", " + model.extensionsRequired[i];
		}
		return false;
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
	for (const tinygltf::Material& material : model.materials) {
		GltfPreviewMaterialMeta meta;
		float opacity = 1.0f;
		float transmission = 0.0f;
		scene.materials.push_back(convertMaterial(model, material, meta, opacity, transmission));
		scene.materialMeta.push_back(meta);
		scene.materialOpacity.push_back(opacity);
		scene.materialTransmission.push_back(transmission);
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

	buildSceneAcceleration(scene);
	scene.loaded = true;

	std::ostringstream out;
	out << "glTF model ready: " << scene.stats.primitiveCount << " primitives, "
		<< scene.stats.triangleCount << " triangles, "
		<< scene.stats.materialCount << " materials";
	if (!warning.empty()) {
		out << " (warning: " << warning << ")";
	}
	scene.status = out.str();
	return true;
}
