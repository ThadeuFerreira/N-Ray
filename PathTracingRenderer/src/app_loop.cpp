#include <app.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <exception>
#include <cstdio>
#include <imgui.h>
#include <iostream>
#include <string>
#include <rlImGui.h>

namespace {
void frameVulkanPreviewModel(RuntimeResources& runtime);

std::string trimWhitespace(std::string text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
		text.erase(text.begin());
	}
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
		text.pop_back();
	}
	return text;
}

// First line of a (possibly multi-line) status string, for compact UI display.
std::string firstLine(const std::string& value) {
	std::size_t newline = value.find('\n');
	return newline == std::string::npos ? value : value.substr(0, newline);
}

// Copy a path into a fixed-size UI buffer, guaranteeing null-termination even
// when the source is longer than the buffer (std::strncpy does not terminate).
void copyToPathBuffer(char* modelFolderPath, size_t pathCapacity, const std::string& value) {
	if (pathCapacity == 0) {
		return;
	}
	std::strncpy(modelFolderPath, value.c_str(), pathCapacity - 1);
	modelFolderPath[pathCapacity - 1] = '\0';
}

void logModelImportUi(const std::string& message) {
	std::cerr << "[VulkanModelImport:UI] " << message << '\n';
}

bool runFolderPickerCommand(const char* command, std::string& selectedPath) {
	if (command == nullptr) {
		return false;
	}

#ifdef _WIN32
	FILE* pipe = _popen(command, "r");
#else
	FILE* pipe = popen(command, "r");
#endif
	if (!pipe) {
		return false;
	}

	char buffer[512];
	std::string output;
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
		output += buffer;
	}

#ifdef _WIN32
	int exitCode = _pclose(pipe);
#else
	int exitCode = pclose(pipe);
#endif
	if (exitCode != 0) {
		return false;
	}

	selectedPath = trimWhitespace(output);
	return !selectedPath.empty();
}

bool browseFolderByOsDialog(std::string& selectedPath) {
#ifdef _WIN32
	const char* command = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $dialog = New-Object System.Windows.Forms.FolderBrowserDialog; $dialog.Description = 'Select glTF model folder'; if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Write-Output $dialog.SelectedPath }\"";
#elif defined(__APPLE__)
	const char* command = "osascript -e 'POSIX path of (choose folder with prompt \"Select glTF model folder\")'";
#else
#if __linux__
	const char* command = "zenity --file-selection --directory --title=\"Select glTF model folder\" 2>/dev/null";
	const char* fallback = "kdialog --getexistingdirectory \"$HOME\" --title \"Select glTF model folder\" 2>/dev/null";
#else
	const char* command = "";
#endif
#endif

#if defined(__linux__)
	if (runFolderPickerCommand(command, selectedPath)) {
		return true;
	}
	return runFolderPickerCommand(fallback, selectedPath);
#else
	if (command[0] == '\0') {
		return false;
	}
	return runFolderPickerCommand(command, selectedPath);
#endif
}

void applyClipboardToModelFolder(char* modelFolderPath, size_t pathCapacity) {
	const char* clipboard = ImGui::GetClipboardText();
	if (clipboard == nullptr) {
		return;
	}
	copyToPathBuffer(modelFolderPath, pathCapacity, trimWhitespace(clipboard));
}

void setModelFolderPath(char* modelFolderPath, size_t pathCapacity, const std::string& folderPath) {
	copyToPathBuffer(modelFolderPath, pathCapacity, folderPath);
}

// Load a model index into GPU buffers and, if it succeeded, make sure the
// model-preview shader is active so the result is visible. Shared by the model
// combo and every import entry point. Returns true when the model is live.
bool activateModelPreview(RuntimeResources& runtime, int modelIndex, std::string& importStatus) {
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
	resetRenderStats(params);
	params.renderInvalidated = true;
	params.shouldSample = false;
	if (!runtime.vulkanPreview.setModel(modelIndex)) {
		importStatus = firstLine(runtime.vulkanPreview.statusMessage());
		logModelImportUi("activate failed index=" + std::to_string(modelIndex) + " status=\"" + importStatus + "\"");
		return false;
	}

	// A freshly imported model is loaded while some other shader is active; the
	// model-preview shader can only be selected once its buffers are ready, so
	// switch to it here now that setModel has uploaded them.
	if (!VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex())) {
		int previewShader = VulkanComputePreview::modelPreviewShaderIndex();
		if (previewShader >= 0) {
			runtime.vulkanPreview.setShader(previewShader);
		}
	}
	logModelImportUi("activated model index=" + std::to_string(modelIndex) + " name=\"" + VulkanComputePreview::modelName(modelIndex) + "\"");
	frameVulkanPreviewModel(runtime);
	importStatus = firstLine(runtime.vulkanPreview.statusMessage());
	return true;
}

void importModelFolderFromUiPath(RuntimeResources& runtime, char* modelFolderPath, std::string& importStatus) {
	std::string requestedPath = modelFolderPath != nullptr ? modelFolderPath : "";
	logModelImportUi("import requested path=\"" + requestedPath + "\" shaderIndex=" + std::to_string(runtime.vulkanPreview.shaderIndex()));
	int importedModel = runtime.vulkanPreview.importModelFromFolder(modelFolderPath);
	importStatus = firstLine(runtime.vulkanPreview.statusMessage());
	logModelImportUi("import returned index=" + std::to_string(importedModel) + " status=\"" + importStatus + "\"");
	if (importedModel >= 0) {
		activateModelPreview(runtime, importedModel, importStatus);
	}
}

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
	settings.maxSamples = std::clamp(params.maxSamples, kMinSamples, kMaxSamples);
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

	GpuStats gpuStats = runtime.vulkanPreview.gpuStats();
	resetRenderStats(params);
	params.currentSample = static_cast<int>(runtime.vulkanPreview.samplesAccumulated());
	params.renderStatsSample = params.currentSample;
	params.renderStatsActive = modelPreview && !runtime.vulkanPreview.converged();
	params.renderStatsComplete = runtime.vulkanPreview.converged();
	if (modelPreview) {
		params.renderStatsTotalRays = gpuStats.primaryRaysTraced;
		params.renderStatsRaysPerSec = gpuStats.primaryRaysPerSec;
		params.renderStatsMsPerSample = gpuStats.gpuDispatchMs;
		params.renderStatsPublishedFrames = gpuStats.frameCount;
		if (gpuStats.primaryRaysTraced > 0 && gpuStats.primaryRaysPerSec > 0.0) {
			params.renderStatsElapsedSec =
				static_cast<double>(gpuStats.primaryRaysTraced) / gpuStats.primaryRaysPerSec;
		}
		if (gpuStats.gpuDispatchMs > 0.0) {
			params.renderStatsSamplesPerSec = 1000.0 / gpuStats.gpuDispatchMs;
		}
	}
	drawRenderTexture(runtime.render, screen);
	return true;
}

bool updatePathTraceRender(RuntimeResources& runtime) {
	params.maxSamples = std::clamp(params.maxSamples, kMinSamples, kMaxSamples);
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
			ImGui::Text("Primary rays: %.2f M", static_cast<double>(gs.primaryRaysTraced) / 1000000.0);
			if (gs.primaryRaysPerSec > 0.0) {
				ImGui::Text("Primary rays/sec: %.2f M", gs.primaryRaysPerSec / 1000000.0);
			}
			else {
				ImGui::TextDisabled("Primary rays/sec: unavailable");
			}
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
	ImGui::SetNextWindowSize(ImVec2(420.0f, 190.0f), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(650.0f, 20.0f), ImGuiCond_Once);
	ImGui::Begin("Vulkan Mode");
	static char modelFolderPath[512] = {};
	static std::string modelFolderImportStatus;

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

			// The model list only changes on import, so rebuild the '\0'-delimited
			// label buffer when the count changes instead of every frame.
			static std::string modelLabels;
			static int cachedModelCount = -1;
			if (cachedModelCount != modelCount) {
				modelLabels.clear();
				for (int i = 0; i < modelCount; i++) {
					modelLabels += VulkanComputePreview::modelName(i);
					modelLabels += '\0';
				}
				modelLabels += '\0';
				cachedModelCount = modelCount;
			}

			if (modelCount == 0) {
				ImGui::TextDisabled("No models imported yet");
			}
			else if (ImGui::Combo("##model", &model, modelLabels.c_str())) {
				logModelImportUi("combo requested model index=" + std::to_string(model) + " name=\"" + VulkanComputePreview::modelName(model) + "\"");
				activateModelPreview(runtime, model, modelFolderImportStatus);
			}

			// Live PBR-factor overrides for the active model. The imported
			// factors multiply any bound textures, so these let an over-metallic
			// or flat/grayscale-albedo asset be corrected without editing the
			// glTF (e.g. drop Metalness to turn chrome back into diffuse).
			int materialCount = runtime.vulkanPreview.materialCount();
			if (materialCount > 0) {
				ImGui::Separator();
				ImGui::TextUnformatted("Material overrides:");
				static int selectedMaterial = 0;
				if (selectedMaterial >= materialCount) {
					selectedMaterial = 0;
				}
				if (materialCount > 1) {
					ImGui::SliderInt("Material", &selectedMaterial, 0, materialCount - 1);
				}
				VulkanPreviewMaterialState matState;
				if (runtime.vulkanPreview.materialState(selectedMaterial, matState)) {
					bool changed = false;
					changed |= ImGui::SliderFloat("Metalness", &matState.metalness, 0.0f, 1.0f);
					if (matState.hasMetallicRoughnessTexture) {
						ImGui::SameLine();
						ImGui::TextDisabled("(x tex)");
					}
					changed |= ImGui::SliderFloat("Roughness", &matState.roughness, 0.0f, 1.0f);
					if (matState.hasMetallicRoughnessTexture) {
						ImGui::SameLine();
						ImGui::TextDisabled("(x tex)");
					}
					changed |= ImGui::ColorEdit3("Base color", &matState.baseColor.x);
					if (matState.hasBaseColorTexture) {
						ImGui::SameLine();
						ImGui::TextDisabled("(x tex)");
					}
					if (changed) {
						runtime.vulkanPreview.setMaterialState(selectedMaterial, matState);
					}
					if (ImGui::Button("Reset materials")) {
						runtime.vulkanPreview.resetMaterialStates();
					}
				}
			}
		}
		ImGui::Spacing();
		ImGui::Text("Import model folder:");
		if (ImGui::InputText("##modelFolderPath", modelFolderPath, sizeof(modelFolderPath), ImGuiInputTextFlags_EnterReturnsTrue)) {
			importModelFolderFromUiPath(runtime, modelFolderPath, modelFolderImportStatus);
		}
		ImGui::SameLine();
		if (ImGui::Button("Paste")) {
			applyClipboardToModelFolder(modelFolderPath, sizeof(modelFolderPath));
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse")) {
			std::string selectedFolder;
			if (browseFolderByOsDialog(selectedFolder) && !selectedFolder.empty()) {
				setModelFolderPath(modelFolderPath, sizeof(modelFolderPath), selectedFolder);
				importModelFolderFromUiPath(runtime, modelFolderPath, modelFolderImportStatus);
			}
			else {
				modelFolderImportStatus = "Browse folder cancelled or unavailable on this OS";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Import Model Folder")) {
			importModelFolderFromUiPath(runtime, modelFolderPath, modelFolderImportStatus);
		}
		if (!modelFolderImportStatus.empty()) {
			ImGui::TextWrapped("Import: %s", modelFolderImportStatus.c_str());
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
