#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct RenderPixel {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 255;
};

struct RenderEnvironment {
	const float* pixels = nullptr;
	int width = 0;
	int height = 0;
	int channels = 3;

	bool isValid() const {
		return pixels != nullptr && width > 0 && height > 0 && channels >= 3;
	}
};

inline RenderPixel vec3ToRenderPixel(const glm::vec3& c) {
	return RenderPixel{
		static_cast<uint8_t>(c.x * 255.0f),
		static_cast<uint8_t>(c.y * 255.0f),
		static_cast<uint8_t>(c.z * 255.0f),
		255
	};
}
