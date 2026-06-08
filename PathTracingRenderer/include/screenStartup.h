#pragma once

#include <glm/glm.hpp>
#include <raylib.h>
#include <render_types.h>

struct Screen {

	float screenSizeX = 0.0f;
	float screenSizeY = 0.0f;
	float viewportX = 0.0f;
	float viewportY = 0.0f;

	float ratio = 0.0f;

	int resX = 0;
	int resY = 0;

	Screen(float screenSizeX, float screenSizeY) :
		screenSizeX(screenSizeX), screenSizeY(screenSizeY) {
	}

	void initScreen(
		int& res,
		float displayX,
		float displayY,
		float displayWidth,
		float displayHeight,
		std::vector<RenderPixel>& framebuffer,
		std::vector<glm::vec3>& accumBuffer
	) {
		viewportX = displayX;
		viewportY = displayY;
		screenSizeX = displayWidth > 1.0f ? displayWidth : 1.0f;
		screenSizeY = displayHeight > 1.0f ? displayHeight : 1.0f;

		ratio = screenSizeX / screenSizeY;

		resX = res;
		resY = static_cast<int>(float(res) / ratio);

		framebuffer.resize(resX * resY);
		accumBuffer.resize(resX * resY);
	}

	void initScreen(
		int& res,
		std::vector<RenderPixel>& framebuffer,
		std::vector<glm::vec3>& accumBuffer
	) {
		initScreen(
			res,
			0.0f,
			0.0f,
			float(GetScreenWidth()),
			float(GetScreenHeight()),
			framebuffer,
			accumBuffer
		);
	}

};
