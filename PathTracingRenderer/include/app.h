#pragma once

#include <vector>
#include <raylib.h>
#include <globalParams.h>
#include <screenStartup.h>
#include <renderer.h>
#include <camera.h>
#include <bvh.h>
#include <ui.h>
#include <mouseRay.h>
#include <render_worker.h>
#include <vulkan_compute_preview.h>

struct RuntimeResources {
	int prevRes = 0;
	Texture2D render = {};
	Image hdri = {};
	AsyncRenderWorker renderWorker;
	VulkanComputePreview vulkanPreview;
	std::vector<RenderPixel> asyncFrame;
	std::vector<glm::vec3> asyncAccum;
	std::vector<RenderPixel> vulkanFrame;
	int asyncRaysPerPixel = 1;
};

extern Params params;
extern Data data;
extern Screen screen;
extern PathTracer pt;
extern PTCam myCam;
extern UI ui;
extern MouseRay mRayGen;
extern Camera3D cam3D;

void configureApplication();
void startupWindow();
void shutdownWindow();

void loadSceneLayer();
void initializeRenderLayer();
void startupRuntimeLayer(RuntimeResources& runtime);
void shutdownRuntimeLayer(RuntimeResources& runtime);

void runMainLoop(RuntimeResources& runtime);

void createFlatBVH();
Texture2D createRenderTexture();
RenderEnvironment makeRenderEnvironment(const Image& image);
void updateCamera3D();
void drawRasterPreview();
void mousePosDisplay();
void traceDebugRay(const RenderEnvironment& environment);
void setDofDist();
void selectModel();
