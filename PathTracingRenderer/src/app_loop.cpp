#include <app.h>

#include <imgui.h>
#include <rlImGui.h>

namespace {
void updateUiHoverState() {
	params.isMouseHoveringUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}

void rebuildRenderTarget(RuntimeResources& runtime) {
	runtime.prevRes = params.res;
	runtime.renderWorker.requestCancel();
	params.currentSample = 0;
	resetRenderStats(params);
	screen.initScreen(params.res, data.frameBuffer, data.accumBuffer);

	UnloadTexture(runtime.render);
	runtime.render = createRenderTexture();

	params.shouldSample = false;
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

void handleViewportActions() {
	if (myCam.clickDof && IsMouseButtonPressed(0) && !params.isMouseHoveringUI) {
		setDofDist();
		params.shouldSample = false;
	}

	if (params.enableSelection) {
		selectModel();
	}
}

void updatePathTraceRender(RuntimeResources& runtime) {
	if (!params.render) {
		runtime.renderWorker.requestCancel();
		runtime.renderWorker.joinFinished();
		resetRenderStats(params);
		return;
	}

	runtime.renderWorker.joinFinished();

	if (!params.shouldSample) {
		runtime.renderWorker.requestCancel();
		params.currentSample = 0;
		resetRenderStats(params);
		params.renderStatsActive = runtime.renderWorker.isRunning();
	}
	else {
		if (!runtime.renderWorker.isRunning() && params.currentSample < params.maxSamples) {
			runtime.renderWorker.start(params, data, myCam, screen, makeRenderEnvironment(runtime.hdri), globalCompactBVH);
		}

		int frameSample = 0;
		int frameResX = 0;
		int frameResY = 0;
		if (runtime.renderWorker.consumeFrame(runtime.asyncFrame, frameSample, frameResX, frameResY)) {
			if (frameResX == screen.resX && frameResY == screen.resY) {
				params.currentSample = frameSample;
				UpdateTexture(runtime.render, runtime.asyncFrame.data());
			}
		}

		applyRenderStats(params, runtime.renderWorker.stats());
	}

	drawRenderTexture(runtime.render, screen);
}

void updateCamera() {
	myCam.cameraLogic(params, screen.ratio);
	updateCamera3D();
}

void drawViewport(RuntimeResources& runtime) {
	BeginMode3D(cam3D);

	if (params.enableDebugRay) {
		traceDebugRay(makeRenderEnvironment(runtime.hdri));
	}

	if (!params.render) {
		drawRasterPreview();
	}

	EndMode3D();
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
		updatePathTraceRender(runtime);
		drawViewport(runtime);

		params.shouldSample = true;
		ui.logic(params, data, myCam);

		rlImGuiEnd();

		mousePosDisplay();

		EndDrawing();
	}
}
