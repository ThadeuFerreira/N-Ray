#include <app.h>

#include <algorithm>
#include <exception>
#include <imgui.h>
#include <iostream>
#include <rlImGui.h>

namespace {
void updateUiHoverState() {
	params.isMouseHoveringUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}

void rebuildRenderTarget(RuntimeResources& runtime) {
	runtime.prevRes = params.res;
	runtime.renderWorker.shutdown();
	runtime.renderWorker.discardFrame();
	resetRenderStats(params);
	screen.initScreen(params.res, data.frameBuffer, data.accumBuffer);
	runtime.asyncFrame.clear();
	runtime.asyncAccum.clear();
	runtime.asyncRaysPerPixel = params.raysPerPixel;

	UnloadTexture(runtime.render);
	runtime.render = createRenderTexture();
	try {
		if (!runtime.vulkanPreview.resize(screen.resX, screen.resY)) {
			std::cerr << runtime.vulkanPreview.statusMessage() << '\n';
		}
	}
	catch (const std::exception& e) {
		runtime.vulkanPreview.shutdown();
		std::cerr << "Vulkan preview resize failed: " << e.what() << '\n';
	}

	params.shouldSample = false;
	params.renderInvalidated = false;
	params.displayInvalidated = false;
}

void handleRenderTargetResize(RuntimeResources& runtime) {
	if (runtime.prevRes != params.res) {
		rebuildRenderTarget(runtime);
	}
}

void updateSamplingGate() {
	if (params.enableSampling && !params.isMouseHoveringUI) {
		params.shouldSample = true;
	}
}

void suspendCpuRendererForVulkan(RuntimeResources& runtime) {
	runtime.renderWorker.requestCancel();
	runtime.renderWorker.joinFinished();
	runtime.renderWorker.discardFrame();
	runtime.asyncFrame.clear();
	runtime.asyncAccum.clear();
}

void handleViewportActions() {
	if (myCam.clickDof && IsMouseButtonPressed(0) && !params.isMouseHoveringUI) {
		setDofDist();
		params.shouldSample = false;
		params.renderInvalidated = true;
	}

	if (params.enableSelection) {
		selectModel();
	}
}

bool displayedFrameMatchesTarget(const RuntimeResources& runtime) {
	size_t pixelCount = static_cast<size_t>(screen.resX) * static_cast<size_t>(screen.resY);
	return params.currentSample > 0 &&
		runtime.asyncAccum.size() == pixelCount &&
		runtime.asyncFrame.size() == pixelCount;
}

void recomposeDisplayedFrame(RuntimeResources& runtime) {
	if (!displayedFrameMatchesTarget(runtime)) {
		return;
	}

	composeRenderFrame(
		pt,
		params.exposure,
		params.contrast,
		params.currentSample,
		runtime.asyncRaysPerPixel,
		runtime.asyncAccum,
		runtime.asyncFrame,
		1
	);
	UpdateTexture(runtime.render, runtime.asyncFrame.data());
}

bool updateVulkanComputePreview(RuntimeResources& runtime) {
	if (!params.useVulkanPreview || !runtime.vulkanPreview.isAvailable()) {
		return false;
	}

	suspendCpuRendererForVulkan(runtime);

	if (!runtime.vulkanPreview.render(static_cast<float>(GetTime()), runtime.vulkanFrame)) {
		return false;
	}

	UpdateTexture(runtime.render, runtime.vulkanFrame.data());
	resetRenderStats(params);
	params.currentSample = 1;
	params.renderStatsComplete = true;
	drawRenderTexture(runtime.render, screen);
	return true;
}

bool updatePathTraceRender(RuntimeResources& runtime) {
	if (updateVulkanComputePreview(runtime)) {
		return true;
	}

	if (params.useVulkanPreview && runtime.vulkanPreview.isAvailable()) {
		suspendCpuRendererForVulkan(runtime);
		resetRenderStats(params);
		return false;
	}

	if (!params.render) {
		runtime.renderWorker.shutdown();
		runtime.renderWorker.discardFrame();
		resetRenderStats(params);
		runtime.asyncAccum.clear();
		return false;
	}

	runtime.renderWorker.updateDisplaySettings(params, params.displayInvalidated);
	runtime.renderWorker.joinFinished();

	if (params.displayInvalidated) {
		recomposeDisplayedFrame(runtime);
		params.displayInvalidated = false;
	}

	if (params.renderInvalidated) {
		runtime.renderWorker.requestCancel();
		runtime.renderWorker.joinFinished();

		if (runtime.renderWorker.isRunning()) {
			params.renderStatsActive = true;
			drawRenderTexture(runtime.render, screen);
			return false;
		}

		runtime.asyncFrame.clear();
		runtime.asyncAccum.clear();
		runtime.renderWorker.discardFrame();
		resetRenderStats(params);
		params.renderInvalidated = false;
	}

	int frameSample = 0;
	int frameRaysPerPixel = 1;
	int frameResX = 0;
	int frameResY = 0;
	if (runtime.renderWorker.consumeFrame(runtime.asyncFrame, runtime.asyncAccum, frameSample, frameRaysPerPixel, frameResX, frameResY)) {
		if (frameResX == screen.resX && frameResY == screen.resY) {
			runtime.asyncRaysPerPixel = frameRaysPerPixel;
			params.currentSample = frameSample;
			UpdateTexture(runtime.render, runtime.asyncFrame.data());
		}
	}

	RenderStatsSnapshot stats = runtime.renderWorker.stats();
	if (params.shouldSample && !runtime.renderWorker.isRunning() && stats.sample < params.maxSamples) {
		runtime.renderWorker.start(params, data, myCam, screen, makeRenderEnvironment(runtime.hdri), globalCompactBVH);
	}

	applyRenderStats(params, stats);
	drawRenderTexture(runtime.render, screen);
	return false;
}

void updateCamera() {
	myCam.cameraLogic(params, screen.ratio);
	updateCamera3D();
}

void drawViewport(RuntimeResources& runtime, bool vulkanFrameDrawn) {
	BeginMode3D(cam3D);

	if (params.enableDebugRay) {
		traceDebugRay(makeRenderEnvironment(runtime.hdri));
	}

	if (!params.render && !vulkanFrameDrawn) {
		drawRasterPreview();
	}

	EndMode3D();
}

void drawVulkanPreviewPanel(RuntimeResources& runtime, bool vulkanFrameDrawn) {
	ImGui::SetNextWindowSize(ImVec2(420.0f, 340.0f), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(220.0f, 20.0f), ImGuiCond_Once);
	ImGui::Begin("Vulkan Compute");

	ImGui::Text("Preview: %s", params.useVulkanPreview ? "enabled" : "disabled");
	ImGui::TextWrapped("%s", runtime.vulkanPreview.statusMessage().c_str());

	if (runtime.vulkanPreview.isAvailable()) {
		ImGui::Text("Resolution: %d x %d", runtime.vulkanPreview.width(), runtime.vulkanPreview.height());
		if (!params.useVulkanPreview || !vulkanFrameDrawn) {
			ImGui::TextDisabled("Preview image is not current this frame");
		}
		ImVec2 available = ImGui::GetContentRegionAvail();
		float aspect = static_cast<float>(runtime.vulkanPreview.width()) / static_cast<float>(std::max(runtime.vulkanPreview.height(), 1));
		float imageWidth = std::max(1.0f, available.x);
		float imageHeight = imageWidth / aspect;
		float maxHeight = std::max(1.0f, available.y);
		if (imageHeight > maxHeight) {
			imageHeight = maxHeight;
			imageWidth = imageHeight * aspect;
		}
		rlImGuiImageSizeV(&runtime.render, Vector2{ imageWidth, imageHeight });
	}

	ImGui::End();
}
}

void runMainLoop(RuntimeResources& runtime) {
	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(BLACK);

		params.dt = GetFrameTime();

		rlImGuiBegin();

		updateUiHoverState();
		handleRenderTargetResize(runtime);
		updateSamplingGate();
		handleViewportActions();
		updateCamera();
		bool vulkanFrameDrawn = updatePathTraceRender(runtime);
		drawViewport(runtime, vulkanFrameDrawn);

		params.shouldSample = true;
		ui.logic(params, data, myCam);
		drawVulkanPreviewPanel(runtime, vulkanFrameDrawn);

		rlImGuiEnd();

		mousePosDisplay();

		EndDrawing();
	}
}
