#include <app.h>

Params params;
Data data;
Screen screen{ float(params.screenSize.x), float(params.screenSize.y) };
PathTracer pt;
PTCam myCam;
UI ui;
MouseRay mRayGen;
Camera3D cam3D;

std::vector<BVH> globalBVH;
std::vector<CompactBVH> globalCompactBVH;

void configureApplication() {
	SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
	SetTraceLogLevel(LOG_NONE);
}

void startupWindow() {
	InitWindow(params.screenSize.x, params.screenSize.y, "Path Tracing");
}

void shutdownWindow() {
	CloseWindow();
}

Texture2D createRenderTexture() {
	Image ptData = {
		.data = data.frameBuffer.data(),
		.width = screen.resX,
		.height = screen.resY,
		.mipmaps = 1,
		.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
	};
	return LoadTextureFromImage(ptData);
}

RenderEnvironment makeRenderEnvironment(const Image& image) {
	if (image.format != PIXELFORMAT_UNCOMPRESSED_R32G32B32) {
		return {};
	}

	return RenderEnvironment{
		static_cast<const float*>(image.data),
		image.width,
		image.height,
		3,
		true
	};
}
