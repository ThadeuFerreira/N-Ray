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
#include <ui_layout.h>

namespace {
void frameVulkanPreviewModel(RuntimeResources& runtime);

VulkanPreviewShadowMode gVulkanPreviewShadowMode = VulkanPreviewShadowMode::RayTraced;
char gModelFolderPath[512] = {};
std::string gModelFolderImportStatus;

bool vulkanMaterialPanelVisible(const RuntimeResources& runtime) {
	return runtime.vulkanPreview.isAvailable() &&
		VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex()) &&
		runtime.vulkanPreview.materialCount() > 0;
}

UiLayout makeUiLayout(const RuntimeResources& runtime) {
	float windowWidth = float(GetScreenWidth());
	float windowHeight = float(GetScreenHeight());
	float menuHeight = ImGui::GetFrameHeight();
	bool showMaterialPanel = vulkanMaterialPanelVisible(runtime);
	float bottomHeight = showMaterialPanel ? kUiMaterialPanelHeight : 0.0f;
	float panelHeight = std::max(1.0f, windowHeight - menuHeight);

	float viewportX = kUiSettingsPanelWidth;
	float viewportY = menuHeight;
	float viewportWidth = std::max(1.0f, windowWidth - kUiSettingsPanelWidth - kUiStatsPanelWidth);
	float viewportHeight = std::max(1.0f, windowHeight - menuHeight - bottomHeight);

	UiLayout layout{};
	layout.settingsPanel = { 0.0f, menuHeight, kUiSettingsPanelWidth, panelHeight };
	layout.statsPanel = { windowWidth - kUiStatsPanelWidth, menuHeight, kUiStatsPanelWidth, panelHeight };
	layout.viewport = { viewportX, viewportY, viewportWidth, viewportHeight };
	layout.materialPanelVisible = showMaterialPanel;
	layout.materialPanel = {
		viewportX,
		std::max(menuHeight, windowHeight - bottomHeight),
		viewportWidth,
		std::max(1.0f, bottomHeight)
	};
	return layout;
}

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
	std::string requestedPath = trimWhitespace(modelFolderPath != nullptr ? modelFolderPath : "");
	if (requestedPath.empty()) {
		importStatus = "Import path is empty";
		return;
	}
	logModelImportUi("import requested path=\"" + requestedPath + "\" shaderIndex=" + std::to_string(runtime.vulkanPreview.shaderIndex()));
	int importedModel = runtime.vulkanPreview.importModelFromFolder(requestedPath);
	importStatus = firstLine(runtime.vulkanPreview.statusMessage());
	logModelImportUi("import returned index=" + std::to_string(importedModel) + " status=\"" + importStatus + "\"");
	if (importedModel >= 0) {
		activateModelPreview(runtime, importedModel, importStatus);
	}
}

void updateUiHoverState() {
	params.isMouseHoveringUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}

void rebuildRenderTarget(RuntimeResources& runtime, const Rectangle& viewport) {
	runtime.prevRes = params.res;
	runtime.renderWorker.shutdown();
	runtime.renderWorker.discardFrame();
	resetRenderStats(params);
	screen.initScreen(
		params.res,
		viewport.x,
		viewport.y,
		viewport.width,
		viewport.height,
		data.frameBuffer,
		data.accumBuffer
	);
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

void handleRenderTargetResize(RuntimeResources& runtime, const Rectangle& viewport) {
	bool viewportChanged =
		std::fabs(screen.viewportX - viewport.x) > 0.5f ||
		std::fabs(screen.viewportY - viewport.y) > 0.5f ||
		std::fabs(screen.screenSizeX - viewport.width) > 0.5f ||
		std::fabs(screen.screenSizeY - viewport.height) > 0.5f;
	if (runtime.prevRes != params.res || viewportChanged) {
		rebuildRenderTarget(runtime, viewport);
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
	settings.shadowMode = gVulkanPreviewShadowMode;
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
	bool shouldUseVulkanPreview = params.useVulkanPreview && !params.render && runtime.vulkanPreview.isAvailable();
	if (shouldUseVulkanPreview && updateVulkanComputePreview(runtime)) {
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
	BeginScissorMode(
		static_cast<int>(screen.viewportX),
		static_cast<int>(screen.viewportY),
		static_cast<int>(screen.screenSizeX),
		static_cast<int>(screen.screenSizeY)
	);
	BeginMode3D(cam3D);

	if (params.enableDebugRay) {
		traceDebugRay(makeRenderEnvironment(runtime.hdri));
	}

	bool cpuActive = params.render || (params.useVulkanPreview && !runtime.vulkanPreview.isAvailable());
	if (!cpuActive && !vulkanFrameDrawn) {
		drawRasterPreview();
	}

	EndMode3D();
	EndScissorMode();
}

void setVulkanShader(RuntimeResources& runtime, int shaderIndex) {
	if (runtime.vulkanPreview.setShader(shaderIndex)) {
		runtime.vulkanFrameValid = false;
		runtime.vulkanFrameDispatched = false;
		if (VulkanComputePreview::isModelPreviewIndex(shaderIndex)) {
			frameVulkanPreviewModel(runtime);
		}
	}
}

void drawVulkanMainMenuBar(RuntimeResources& runtime) {
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	bool vulkanAvailable = runtime.vulkanPreview.isAvailable();

	if (ImGui::BeginMenu("Scene", vulkanAvailable)) {
		if (ImGui::BeginMenu("Load Scene")) {
				int modelCount = VulkanComputePreview::modelCount();
				if (modelCount == 0) {
					ImGui::TextDisabled("No imported scenes");
				}
				for (int i = 0; i < modelCount; i++) {
					bool selected = i == runtime.vulkanPreview.modelIndex();
					if (ImGui::MenuItem(VulkanComputePreview::modelName(i), nullptr, selected)) {
						logModelImportUi("menu requested model index=" + std::to_string(i) + " name=\"" + VulkanComputePreview::modelName(i) + "\"");
						activateModelPreview(runtime, i, gModelFolderImportStatus);
					}
				}
				ImGui::EndMenu();
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Import glTF scene folder");
			ImGui::SetNextItemWidth(440.0f);
			if (ImGui::InputText("##menuModelFolderPath", gModelFolderPath, sizeof(gModelFolderPath), ImGuiInputTextFlags_EnterReturnsTrue)) {
				importModelFolderFromUiPath(runtime, gModelFolderPath, gModelFolderImportStatus);
			}
			if (ImGui::Button("Paste")) {
				applyClipboardToModelFolder(gModelFolderPath, sizeof(gModelFolderPath));
			}
			ImGui::SameLine();
			if (ImGui::Button("Browse")) {
				std::string selectedFolder;
				if (browseFolderByOsDialog(selectedFolder) && !selectedFolder.empty()) {
					setModelFolderPath(gModelFolderPath, sizeof(gModelFolderPath), selectedFolder);
					importModelFolderFromUiPath(runtime, gModelFolderPath, gModelFolderImportStatus);
				}
				else {
					gModelFolderImportStatus = "Browse folder cancelled or unavailable on this OS";
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Import")) {
				importModelFolderFromUiPath(runtime, gModelFolderPath, gModelFolderImportStatus);
			}
			if (!gModelFolderImportStatus.empty()) {
				ImGui::TextWrapped("Import: %s", gModelFolderImportStatus.c_str());
			}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Vulkan", vulkanAvailable)) {
		if (ImGui::BeginMenu("Preview Mode")) {
			int current = runtime.vulkanPreview.shaderIndex();
			int count = VulkanComputePreview::shaderCount();
			for (int i = 0; i < count; i++) {
				if (ImGui::MenuItem(VulkanComputePreview::shaderName(i), nullptr, i == current)) {
					setVulkanShader(runtime, i);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Shadows")) {
			static const char* shadowLabels[] = { "None", "Ray Traced", "Shadow Map" };
			for (int i = 0; i < 3; i++) {
				bool selected = static_cast<int>(gVulkanPreviewShadowMode) == i;
				if (ImGui::MenuItem(shadowLabels[i], nullptr, selected)) {
					gVulkanPreviewShadowMode = static_cast<VulkanPreviewShadowMode>(i);
					runtime.vulkanFrameValid = false;
					runtime.vulkanFrameDispatched = false;
					params.renderInvalidated = true;
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Frame Active Scene")) {
			frameVulkanPreviewModel(runtime);
		}
		ImGui::EndMenu();
	}

	if (vulkanAvailable) {
		ImGui::Separator();
		ImGui::TextDisabled("%s", firstLine(runtime.vulkanPreview.statusMessage()).c_str());
	}

	ImGui::EndMainMenuBar();
}

void drawVulkanMaterialPanel(RuntimeResources& runtime, const UiLayout& layout) {
	if (!layout.materialPanelVisible) {
		return;
	}

	int materialCount = runtime.vulkanPreview.materialCount();
	if (materialCount <= 0) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(layout.materialPanel.x, layout.materialPanel.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(layout.materialPanel.width, layout.materialPanel.height), ImGuiCond_Always);
	ImGui::Begin("Vulkan Materials", nullptr, kLockedPanelFlags);

	static int selectedMaterial = 0;
	if (selectedMaterial >= materialCount) {
		selectedMaterial = 0;
	}

	VulkanPreviewMaterialState matState;
	if (!runtime.vulkanPreview.materialState(selectedMaterial, matState)) {
		ImGui::TextDisabled("No editable material state");
		ImGui::End();
		return;
	}

	ImGuiTableFlags tableFlags =
		ImGuiTableFlags_SizingStretchProp |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_Resizable;
	if (ImGui::BeginTable("VulkanMaterialOverrides", 3, tableFlags)) {
		ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthFixed, 300.0f);
		ImGui::TableSetupColumn("Surface", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Light/Optics", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted("Material");
		if (materialCount > 1) {
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderInt("##vulkanMaterialIndex", &selectedMaterial, 0, materialCount - 1);
		}
		std::string label = matState.name.empty()
			? matState.semanticLabel
			: (matState.semanticLabel.empty()
				? matState.name
				: matState.name + " (" + matState.semanticLabel + ")");
		if (!label.empty()) {
			ImGui::TextWrapped("%s", label.c_str());
		}
		if (ImGui::Button("Reset materials")) {
			runtime.vulkanPreview.resetMaterialStates();
		}

		bool changed = false;
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Surface");
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::ColorEdit3("Albedo##vulkanMaterial", &matState.baseColor.x);
		if (matState.hasBaseColorTexture) {
			ImGui::SameLine();
			ImGui::TextDisabled("(x tex)");
		}
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::SliderFloat("Alpha##vulkanMaterial", &matState.alpha, 0.0f, 1.0f);
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::SliderFloat("Metalness##vulkanMaterial", &matState.metalness, 0.0f, 1.0f);
		if (matState.hasMetallicRoughnessTexture) {
			ImGui::SameLine();
			ImGui::TextDisabled("(x tex)");
		}
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::SliderFloat("Roughness##vulkanMaterial", &matState.roughness, 0.0f, 1.0f);
		if (matState.hasMetallicRoughnessTexture) {
			ImGui::SameLine();
			ImGui::TextDisabled("(x tex)");
		}

		ImGui::TableSetColumnIndex(2);
		ImGui::TextUnformatted("Light/Optics");
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::ColorEdit3("Emission##vulkanMaterial", &matState.emissiveFactor.x);
		if (matState.hasEmissiveTexture) {
			ImGui::SameLine();
			ImGui::TextDisabled("(x tex)");
		}
		ImGui::SetNextItemWidth(-1.0f);
		changed |= ImGui::SliderFloat("Intensity##vulkanMaterial", &matState.emissiveIntensity, 0.0f, 20.0f);
		if (matState.isTransmission) {
			ImGui::SetNextItemWidth(-1.0f);
			changed |= ImGui::SliderFloat("Transmission##vulkanMaterial", &matState.transmission, 0.0f, 1.0f);
			ImGui::SetNextItemWidth(-1.0f);
			changed |= ImGui::SliderFloat("IOR##vulkanMaterial", &matState.ior, 1.0f, 2.5f);
		}
		if (matState.hasNormalTexture) {
			ImGui::SetNextItemWidth(-1.0f);
			changed |= ImGui::SliderFloat("Normal Scale##vulkanMaterial", &matState.normalScale, 0.0f, 4.0f);
		}

		if (changed) {
			runtime.vulkanPreview.setMaterialState(selectedMaterial, matState);
		}

		ImGui::EndTable();
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

		UiLayout layout = makeUiLayout(runtime);
		updateUiHoverState();
		handleRenderTargetResize(runtime, layout.viewport);
		updateSamplingGate();
		handleViewportActions();
		updateCamera();
		bool vulkanFrameDrawn = updatePathTraceRender(runtime);
		drawViewport(runtime, vulkanFrameDrawn);

		params.shouldSample = true;
		drawVulkanMainMenuBar(runtime);
		ui.logic(params, data, myCam, layout);
		drawVulkanMaterialPanel(runtime, layout);

		rlImGuiEnd();

		if (params.enableDebugRay && !params.isMouseHoveringUI) {
			mousePosDisplay();
		}

		EndDrawing();
	}
}
