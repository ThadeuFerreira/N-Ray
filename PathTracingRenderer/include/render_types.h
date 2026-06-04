#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
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
	bool isFloatRgb32 = false;

	bool isValid() const {
		return pixels != nullptr && width > 0 && height > 0 && channels >= 3 && isFloatRgb32;
	}
};

inline RenderPixel vec3ToRenderPixel(const glm::vec3& c) {
	auto pack = [](float value) -> uint8_t {
		if (!std::isfinite(value)) {
			return 0;
		}

		value = std::clamp(value, 0.0f, 1.0f);
		return static_cast<uint8_t>(value * 255.0f);
	};

	return RenderPixel{
		pack(c.x),
		pack(c.y),
		pack(c.z),
		255
	};
}

static_assert(sizeof(RenderPixel) == 4, "RenderPixel must match R8G8B8A8 texture uploads.");
