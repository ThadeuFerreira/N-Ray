#include <app.h>

#include <algorithm>
#include <cmath>
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
	runtime.vulkanFrame.clear();
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
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

VulkanPreviewCamera makeVulkanPreviewCamera() {
	VulkanPreviewCamera camera{};
	camera.position = myCam.camPos;
	camera.forward = myCam.camNormal;
	camera.right = myCam.right;
	camera.up = myCam.up;
	camera.verticalScale = myCam.verticalScale;
	camera.aspect = screen.resY > 0 ? static_cast<float>(screen.resX) / static_cast<float>(screen.resY) : 1.0f;
	return camera;
}

VulkanPreviewSettings makeVulkanPreviewSettings() {
	VulkanPreviewSettings settings{};
	settings.maxSamples = std::max(1, params.maxSamples);
	settings.maxBounces = std::max(0, params.maxBounces);
	settings.rrMinBounces = std::max(0, params.rrMinBounces);
	settings.russianRoulette = params.russianRoulette;
	settings.exposure = params.exposure;
	settings.contrast = params.contrast;
	settings.skyIntensity = params.skyIntensity;
	settings.enableSky = params.enableSky;
	settings.enableSun = params.enableSun;
	settings.sunDir = params.sunDir;
	settings.sunAngle = params.sunAngle;
	settings.sunColor = params.sunColor;
	settings.sunIntensity = params.sunIntensity;
	return settings;
}

void frameVulkanPreviewModel(RuntimeResources& runtime) {
	glm::vec3 boundsMin;
	glm::vec3 boundsMax;
	if (!runtime.vulkanPreview.modelBounds(boundsMin, boundsMax)) {
		return;
	}

	glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
	glm::vec3 extent = boundsMax - boundsMin;
	myCam.orbitCenter = center;

	float radius = std::max(
		glm::length(extent) * 0.5f,
		glm::length(center - myCam.orbitCenter) + glm::length(extent) * 0.5f
	);
	if (!std::isfinite(radius) || radius <= 0.0001f) {
		radius = 1.0f;
	}

	float halfFov = glm::radians(myCam.fov) * 0.5f;
	float fovScale = std::max(std::tan(halfFov), 0.1f);
	float distance = (radius / fovScale) * 1.45f;

	myCam.camPos = myCam.orbitCenter + glm::vec3(0.0f, -distance, radius * 0.35f);
	myCam.camTarget = myCam.orbitCenter;
	myCam.targetNormal = glm::normalize(myCam.camTarget - myCam.camPos);
	myCam.focusDist = glm::length(myCam.camTarget - myCam.camPos);
	myCam.targetDist = myCam.focusDist;
	myCam.camSpeed = std::max(1.0f, radius * 0.6f);
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
	params.renderInvalidated = true;
	params.shouldSample = false;
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
	runtime.vulkanFrameValid = false;
}

bool updateVulkanComputePreview(RuntimeResources& runtime) {
	if (!params.useVulkanPreview || !runtime.vulkanPreview.isAvailable()) {
		runtime.vulkanFrameDispatched = false;
		return false;
	}

	suspendCpuRendererForVulkan(runtime);

	size_t pixelCount = static_cast<size_t>(screen.resX) * static_cast<size_t>(screen.resY);
	bool modelPreview = VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex());
	bool resetAccumulation = params.renderInvalidated || runtime.vulkanFrame.size() != pixelCount;
	bool convergedModelFrame =
		modelPreview &&
		runtime.vulkanFrameValid &&
		runtime.vulkanFrame.size() == pixelCount &&
		!resetAccumulation;

	if (!convergedModelFrame) {
		VulkanPreviewSettings settings = makeVulkanPreviewSettings();
		settings.resetAccumulation = resetAccumulation;
		if (!runtime.vulkanPreview.render(static_cast<float>(GetTime()), makeVulkanPreviewCamera(), settings, runtime.vulkanFrame)) {
			runtime.vulkanFrameValid = false;
			runtime.vulkanFrameDispatched = false;
			return false;
		}

		UpdateTexture(runtime.render, runtime.vulkanFrame.data());
		runtime.vulkanFrameValid = modelPreview && runtime.vulkanPreview.converged();
		runtime.vulkanFrameDispatched = true;
		params.renderInvalidated = false;
	}
	else {
		runtime.vulkanFrameDispatched = false;
	}
	resetRenderStats(params);
	params.currentSample = static_cast<int>(runtime.vulkanPreview.samplesAccumulated());
	params.renderStatsSample = params.currentSample;
	params.renderStatsActive = modelPreview && !runtime.vulkanPreview.converged();
	params.renderStatsComplete = runtime.vulkanPreview.converged();
	drawRenderTexture(runtime.render, screen);
	return true;
}

bool updatePathTraceRender(RuntimeResources& runtime) {
	if (updateVulkanComputePreview(runtime)) {
		return true;
	}

	bool cpuFallback = params.useVulkanPreview && !runtime.vulkanPreview.isAvailable();
	if (!params.render && !cpuFallback) {
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
			runtime.vulkanFrameValid = false;
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

	bool cpuActive = params.render || (params.useVulkanPreview && !runtime.vulkanPreview.isAvailable());
	if (!cpuActive && !vulkanFrameDrawn) {
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
		ImGui::Separator();

		GpuStats gs = runtime.vulkanPreview.gpuStats();
		ImGui::Text("Resolution:  %d x %d", runtime.vulkanPreview.width(), runtime.vulkanPreview.height());
		ImGui::Text("Frames:      %u", gs.frameCount);
		if (VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex())) {
			ImGui::Text("Sample:      %u / %u", gs.samplesAccumulated, gs.maxSamples);
		}

		if (gs.timestampAvailable) {
			if (!runtime.vulkanFrameDispatched &&
				runtime.vulkanFrameValid &&
				VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex())) {
				ImGui::Text("GPU dispatch: cached");
				ImGui::Text("GPU load est: 0.0%%");
			}
			else {
				float frameDeltaMs = params.dt * 1000.0f;
				float gpuLoad = frameDeltaMs > 0.0f ? static_cast<float>(gs.gpuDispatchMs / frameDeltaMs * 100.0) : 0.0f;
				ImGui::Text("GPU dispatch: %.3f ms", gs.gpuDispatchMs);
				ImGui::Text("GPU load est: %.1f%%", gpuLoad);
			}
		} else {
			ImGui::TextDisabled("GPU timing: not supported");
		}

		if (gs.localHeapBytes > 0) {
			ImGui::Text("VRAM total:  %.1f MB", gs.localHeapBytes / (1024.0 * 1024.0));
			if (gs.memBudgetAvailable) {
				ImGui::Text("VRAM used:   %.1f MB", gs.localHeapUsed / (1024.0 * 1024.0));
			} else {
				ImGui::TextDisabled("VRAM used: budget ext unavailable");
			}
		}
		ImGui::Text("Pixel buf:   %.1f MB", gs.pixelBufferBytes / (1024.0 * 1024.0));

		ImGui::Separator();

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

void drawShaderSelectorPanel(RuntimeResources& runtime) {
	ImGui::SetNextWindowSize(ImVec2(360.0f, 120.0f), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(650.0f, 20.0f), ImGuiCond_Once);
	ImGui::Begin("Vulkan Mode");

	if (runtime.vulkanPreview.isAvailable()) {
		int current = runtime.vulkanPreview.shaderIndex();
		int count   = VulkanComputePreview::shaderCount();

		// Build label list for ImGui combo
		std::string allLabels;
		for (int i = 0; i < count; i++) {
			allLabels += VulkanComputePreview::shaderName(i);
			allLabels += '\0';
		}
		allLabels += '\0';

		if (ImGui::Combo("##mode", &current, allLabels.c_str())) {
			if (runtime.vulkanPreview.setShader(current)) {
				runtime.vulkanFrameValid = false;
				runtime.vulkanFrameDispatched = false;
				if (VulkanComputePreview::isModelPreviewIndex(current)) {
					frameVulkanPreviewModel(runtime);
				}
			}
		}

		if (VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex())) {
			int model = runtime.vulkanPreview.modelIndex();
			int modelCount = VulkanComputePreview::modelCount();

			std::string modelLabels;
			for (int i = 0; i < modelCount; i++) {
				modelLabels += VulkanComputePreview::modelName(i);
				modelLabels += '\0';
			}
			modelLabels += '\0';

			if (ImGui::Combo("##model", &model, modelLabels.c_str())) {
				runtime.vulkanFrameValid = false;
				runtime.vulkanFrameDispatched = false;
				resetRenderStats(params);
				params.renderInvalidated = true;
				params.shouldSample = false;
				if (runtime.vulkanPreview.setModel(model)) {
					frameVulkanPreviewModel(runtime);
				}
			}
		}
	} else {
		ImGui::TextDisabled("Vulkan unavailable");
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
		drawShaderSelectorPanel(runtime);

		rlImGuiEnd();

		mousePosDisplay();

		EndDrawing();
	}
}
