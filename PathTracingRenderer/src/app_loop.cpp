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
VulkanPreviewShadowMode gVulkanPreviewShadowMode = VulkanPreviewShadowMode::RayTraced;
VulkanDenoiserSettings gVulkanDenoiserSettings{};
VulkanPreviewLightingDebugSettings gVulkanLightingDebugSettings{};
char gModelFolderPath[512] = {};
std::string gModelFolderImportStatus;
char gSkyFilePath[512] = {};
std::string gSkyImportStatus;

bool vulkanMaterialPanelVisible(const RuntimeResources& runtime) {
	return runtime.vulkanPreview.isAvailable() &&
		VulkanComputePreview::isModelPreviewIndex(runtime.vulkanPreview.shaderIndex()) &&
		runtime.vulkanPreview.materialCount() > 0;
}

const char* denoiserModeLabel(VulkanDenoiserMode mode) {
	switch (mode) {
	case VulkanDenoiserMode::Off: return "Off";
	case VulkanDenoiserMode::SpatialAtrous: return "Spatial Atrous";
	case VulkanDenoiserMode::SvgfLite: return "SVGF Lite";
	default: return "Unknown";
	}
}

const char* denoiserDebugViewLabel(VulkanDenoiserDebugView view) {
	switch (view) {
	case VulkanDenoiserDebugView::Final: return "Final";
	case VulkanDenoiserDebugView::RawAccumulation: return "Raw Accumulation";
	case VulkanDenoiserDebugView::DenoisedPreview: return "Denoised Preview";
	case VulkanDenoiserDebugView::Normal: return "Normal";
	case VulkanDenoiserDebugView::Albedo: return "Albedo";
	case VulkanDenoiserDebugView::Depth: return "Depth";
	case VulkanDenoiserDebugView::MaterialId: return "Material Id";
	case VulkanDenoiserDebugView::InstanceId: return "Instance Id";
	default: return "Unknown";
	}
}

const char* opticalModeLabel(VulkanPreviewOpticalMode mode) {
	switch (mode) {
	case VulkanPreviewOpticalMode::ThinTransmission: return "Thin Transmission";
	case VulkanPreviewOpticalMode::VolumeTransmission: return "Volume Transmission";
	default: return "Coverage/Opaque";
	}
}

void invalidateVulkanPostprocess(RuntimeResources& runtime) {
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
	params.displayInvalidated = true;
}

void invalidateVulkanTrace(RuntimeResources& runtime) {
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
	params.renderInvalidated = true;
	params.shouldSample = false;
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
	std::string modelName = modelIndex >= 0 ? VulkanComputePreview::modelName(modelIndex) : "Default Scene";
	logModelImportUi("activated model index=" + std::to_string(modelIndex) + " name=\"" + modelName + "\"");
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

bool browseSkyFileByOsDialog(std::string& selectedPath) {
#ifdef _WIN32
	const char* command = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $dialog = New-Object System.Windows.Forms.OpenFileDialog; $dialog.Filter = 'Environment maps (*.exr;*.hdr)|*.exr;*.hdr'; if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Write-Output $dialog.FileName }\"";
#elif defined(__APPLE__)
	const char* command = "osascript -e 'POSIX path of (choose file with prompt \"Select .exr or .hdr sky\")'";
#else
#if __linux__
	const char* command = "zenity --file-selection --title=\"Select .exr or .hdr sky\" --file-filter=\"Environment maps | *.exr *.hdr\" 2>/dev/null";
	const char* fallback = "kdialog --getopenfilename \"$HOME\" \"*.exr *.hdr\" --title \"Select .exr or .hdr sky\" 2>/dev/null";
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

// Select an environment sky index (-1 = procedural). Resets accumulation so the
// new lighting integrates from scratch.
bool activateSky(RuntimeResources& runtime, int skyIndex, std::string& importStatus) {
	runtime.vulkanFrameValid = false;
	runtime.vulkanFrameDispatched = false;
	if (!runtime.vulkanPreview.setSky(skyIndex)) {
		importStatus = firstLine(runtime.vulkanPreview.statusMessage());
		return false;
	}
	params.renderInvalidated = true;
	params.shouldSample = false;
	importStatus = skyIndex >= 0
		? std::string("Selected sky: ") + VulkanComputePreview::skyName(skyIndex)
		: std::string("Selected procedural sky");
	return true;
}

void importSkyFromUiPath(RuntimeResources& runtime, char* skyFilePath, std::string& importStatus) {
	std::string requestedPath = trimWhitespace(skyFilePath != nullptr ? skyFilePath : "");
	if (requestedPath.empty()) {
		importStatus = "Import path is empty";
		return;
	}
	int importedSky = runtime.vulkanPreview.importSkyFromFile(requestedPath);
	importStatus = firstLine(runtime.vulkanPreview.statusMessage());
	if (importedSky >= 0) {
		activateSky(runtime, importedSky, importStatus);
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

VulkanPreviewSettings makeVulkanPreviewSettings(RuntimeResources& runtime) {
	VulkanPreviewSettings settings{};
	settings.maxSamples = std::clamp(params.maxSamples, kMinSamples, kMaxSamples);
	settings.maxBounces = std::max(0, params.maxBounces);
	settings.rrMinBounces = std::max(0, params.rrMinBounces);
	settings.russianRoulette = params.russianRoulette;
	settings.exposure = params.exposure;
	settings.contrast = params.contrast;
	settings.skyIntensity = params.skyIntensity;
	settings.enableSky = params.enableSky;
	settings.enableEnvironment = params.enableEnvironment;
	settings.enableSun = params.enableSun;
	settings.sunDir = params.sunDir;
	settings.sunAngle = params.sunAngle;
	settings.sunColor = params.sunColor;
	settings.sunIntensity = params.sunIntensity;
	settings.shadowMode = gVulkanPreviewShadowMode;
	settings.denoiser = gVulkanDenoiserSettings;

	// Three-point lighting. Light positions are placed around the active model
	// bounds (Z-up; the preview camera frames the model from -Y, so "front" is
	// -Y, "right" is +X, "up" is +Z). Intensities come from the UI; positions are
	// derived each frame so they track the model size. Falls back to a unit scene
	// centered at the origin when no model bounds are available.
	settings.enableThreePointLighting = params.enableThreePointLighting;
	glm::vec3 center(0.0f);
	float radius = 1.0f;
	glm::vec3 boundsMin;
	glm::vec3 boundsMax;
	if (runtime.vulkanPreview.modelBounds(boundsMin, boundsMax)) {
		center = (boundsMin + boundsMax) * 0.5f;
		float r = 0.5f * glm::length(boundsMax - boundsMin);
		if (std::isfinite(r) && r > 1e-4f) {
			radius = r;
		}
	}

	auto makeLight = [&](const glm::vec3& offset, const glm::vec3& color, float intensity) {
		VulkanPreviewPointLight light;
		light.position = center + offset * radius;
		light.radius = radius;
		light.color = color;
		light.intensity = intensity;
		return light;
	};

	// key: front-left-above (warm), fill: front-right softer (cool), rim: behind-above (neutral).
	settings.keyLight  = makeLight(glm::vec3(-1.3f, -1.5f, 1.3f), glm::vec3(1.0f, 0.95f, 0.85f), params.keyLightIntensity);
	settings.fillLight = makeLight(glm::vec3( 1.5f, -1.2f, 0.6f), glm::vec3(0.8f, 0.88f, 1.0f),  params.fillLightIntensity);
	settings.rimLight  = makeLight(glm::vec3( 0.3f,  1.6f, 1.4f), glm::vec3(1.0f, 1.0f, 1.0f),   params.rimLightIntensity);
	settings.lightingDebug = gVulkanLightingDebugSettings;
	return settings;
}

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

namespace {

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
	bool displayOnlyRedraw =
		modelPreview &&
		params.displayInvalidated &&
		!resetAccumulation &&
		runtime.vulkanFrame.size() == pixelCount &&
		runtime.vulkanPreview.samplesAccumulated() > 0;
	bool convergedModelFrame =
		modelPreview &&
		runtime.vulkanFrameValid &&
		runtime.vulkanFrame.size() == pixelCount &&
		!resetAccumulation &&
		!displayOnlyRedraw;

	if (!convergedModelFrame) {
		VulkanPreviewSettings settings = makeVulkanPreviewSettings(runtime);
		settings.resetAccumulation = resetAccumulation;
		settings.postprocessOnly = displayOnlyRedraw;
		if (!runtime.vulkanPreview.render(static_cast<float>(GetTime()), makeVulkanPreviewCamera(), settings, runtime.vulkanFrame)) {
			runtime.vulkanFrameValid = false;
			runtime.vulkanFrameDispatched = false;
			return false;
		}

		UpdateTexture(runtime.render, runtime.vulkanFrame.data());
		runtime.vulkanFrameValid = modelPreview && runtime.vulkanPreview.converged();
		runtime.vulkanFrameDispatched = true;
		params.renderInvalidated = false;
		params.displayInvalidated = false;
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

	if (params.debugMaterialColors) {
		runtime.renderWorker.shutdown();
		runtime.renderWorker.discardFrame();
		resetRenderStats(params);
		runtime.asyncAccum.clear();
		return false;
	}

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
	if (params.debugMaterialColors || (!cpuActive && !vulkanFrameDrawn)) {
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
			if (ImGui::MenuItem("Default Scene", nullptr, runtime.vulkanPreview.modelIndex() < 0)) {
				logModelImportUi("menu requested default scene");
				activateModelPreview(runtime, -1, gModelFolderImportStatus);
			}
			ImGui::Separator();
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

		ImGui::Separator();
		if (ImGui::BeginMenu("Skies")) {
			if (ImGui::MenuItem("Procedural Sky", nullptr, runtime.vulkanPreview.skyIndex() < 0)) {
				activateSky(runtime, -1, gSkyImportStatus);
			}
			ImGui::Separator();
			int skyCount = VulkanComputePreview::skyCount();
			if (skyCount == 0) {
				ImGui::TextDisabled("No imported skies");
			}
			for (int i = 0; i < skyCount; i++) {
				bool selected = i == runtime.vulkanPreview.skyIndex();
				if (ImGui::MenuItem(VulkanComputePreview::skyName(i), nullptr, selected)) {
					activateSky(runtime, i, gSkyImportStatus);
				}
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Import sky (.exr / .hdr)");
		ImGui::SetNextItemWidth(440.0f);
		if (ImGui::InputText("##menuSkyFilePath", gSkyFilePath, sizeof(gSkyFilePath), ImGuiInputTextFlags_EnterReturnsTrue)) {
			importSkyFromUiPath(runtime, gSkyFilePath, gSkyImportStatus);
		}
		if (ImGui::Button("Paste##sky")) {
			const char* clipboard = ImGui::GetClipboardText();
			if (clipboard != nullptr) {
				copyToPathBuffer(gSkyFilePath, sizeof(gSkyFilePath), trimWhitespace(clipboard));
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse##sky")) {
			std::string selectedFile;
			if (browseSkyFileByOsDialog(selectedFile) && !selectedFile.empty()) {
				copyToPathBuffer(gSkyFilePath, sizeof(gSkyFilePath), selectedFile);
				importSkyFromUiPath(runtime, gSkyFilePath, gSkyImportStatus);
			}
			else {
				gSkyImportStatus = "Browse file cancelled or unavailable on this OS";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Import##sky")) {
			importSkyFromUiPath(runtime, gSkyFilePath, gSkyImportStatus);
		}
		if (!gSkyImportStatus.empty()) {
			ImGui::TextWrapped("Sky: %s", gSkyImportStatus.c_str());
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
					invalidateVulkanTrace(runtime);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Lighting Debug")) {
			bool changed = false;
			changed |= ImGui::Checkbox("3-Point Lighting", &params.enableThreePointLighting);
			changed |= ImGui::Checkbox("Key Light", &gVulkanLightingDebugSettings.keyLightEnabled);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Key Intensity", &params.keyLightIntensity, 0.0f, 64.0f, "%.2f");
			changed |= ImGui::Checkbox("Fill Light", &gVulkanLightingDebugSettings.fillLightEnabled);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Fill Intensity", &params.fillLightIntensity, 0.0f, 64.0f, "%.2f");
			changed |= ImGui::Checkbox("Rim Light", &gVulkanLightingDebugSettings.rimLightEnabled);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Rim Intensity", &params.rimLightIntensity, 0.0f, 64.0f, "%.2f");
			ImGui::Separator();
			changed |= ImGui::Checkbox("Point Light Shadows", &gVulkanLightingDebugSettings.pointLightShadows);
			changed |= ImGui::Checkbox("Direct Diffuse", &gVulkanLightingDebugSettings.directDiffuse);
			changed |= ImGui::Checkbox("Direct Specular", &gVulkanLightingDebugSettings.directSpecular);
			changed |= ImGui::Checkbox("Clearcoat Specular", &gVulkanLightingDebugSettings.clearcoatSpecular);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Specular Scale", &gVulkanLightingDebugSettings.directSpecularScale, 0.0f, 4.0f, "%.2f");
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Point Light Size", &gVulkanLightingDebugSettings.pointLightSizeScale, 0.0f, 2.0f, "%.3f");
			changed |= ImGui::Checkbox("Verbose Lighting Logs", &gVulkanLightingDebugSettings.verboseLogging);
			if (ImGui::Button("Reset Lighting Debug")) {
				gVulkanLightingDebugSettings = VulkanPreviewLightingDebugSettings{};
				changed = true;
			}
			if (changed) {
				invalidateVulkanTrace(runtime);
			}
			GpuStats gpuStats = runtime.vulkanPreview.gpuStats();
			ImGui::Separator();
			ImGui::Text("Sample: %.2f ms", gpuStats.gpuDispatchMs);
			ImGui::Text("Point shadows: %s", gVulkanLightingDebugSettings.pointLightShadows ? "on" : "off");
			ImGui::Text("Denoiser: %s", denoiserModeLabel(gVulkanDenoiserSettings.mode));
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Denoiser")) {
			bool changed = false;
			if (ImGui::BeginMenu("Mode")) {
				bool offSelected = gVulkanDenoiserSettings.mode == VulkanDenoiserMode::Off;
				if (ImGui::MenuItem("Off", nullptr, offSelected)) {
					gVulkanDenoiserSettings.mode = VulkanDenoiserMode::Off;
					changed = true;
				}
				bool spatialSelected = gVulkanDenoiserSettings.mode == VulkanDenoiserMode::SpatialAtrous;
				if (ImGui::MenuItem("Spatial Atrous", nullptr, spatialSelected)) {
					gVulkanDenoiserSettings.mode = VulkanDenoiserMode::SpatialAtrous;
					changed = true;
				}
				ImGui::MenuItem("SVGF Lite", "not implemented", false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Debug View")) {
				for (uint32_t i = 0; i <= static_cast<uint32_t>(VulkanDenoiserDebugView::InstanceId); ++i) {
					VulkanDenoiserDebugView view = static_cast<VulkanDenoiserDebugView>(i);
					bool selected = gVulkanDenoiserSettings.debugView == view;
					if (ImGui::MenuItem(denoiserDebugViewLabel(view), nullptr, selected)) {
						gVulkanDenoiserSettings.debugView = view;
						changed = true;
					}
				}
				ImGui::EndMenu();
			}

			int passCap = static_cast<int>(gVulkanDenoiserSettings.maxAtrousPasses);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::SliderInt("Pass cap", &passCap, 0, 6)) {
				gVulkanDenoiserSettings.maxAtrousPasses = static_cast<uint32_t>(std::clamp(passCap, 0, 6));
				changed = true;
			}
			int fadeStart = static_cast<int>(gVulkanDenoiserSettings.fadeOutStartSample);
			int fadeEnd = static_cast<int>(gVulkanDenoiserSettings.fadeOutEndSample);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::SliderInt("Fade start", &fadeStart, kMinSamples, kMaxSamples)) {
				fadeStart = std::clamp(fadeStart, kMinSamples, kMaxSamples);
				gVulkanDenoiserSettings.fadeOutStartSample = static_cast<uint32_t>(fadeStart);
				if (fadeEnd < fadeStart) {
					gVulkanDenoiserSettings.fadeOutEndSample = static_cast<uint32_t>(fadeStart);
				}
				changed = true;
			}
			fadeEnd = static_cast<int>(gVulkanDenoiserSettings.fadeOutEndSample);
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::SliderInt("Fade end", &fadeEnd, kMinSamples, kMaxSamples)) {
				fadeEnd = std::clamp(fadeEnd, kMinSamples, kMaxSamples);
				if (fadeEnd < static_cast<int>(gVulkanDenoiserSettings.fadeOutStartSample)) {
					fadeEnd = static_cast<int>(gVulkanDenoiserSettings.fadeOutStartSample);
				}
				gVulkanDenoiserSettings.fadeOutEndSample = static_cast<uint32_t>(fadeEnd);
				changed = true;
			}
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Depth sigma", &gVulkanDenoiserSettings.depthSigma, 0.0f, 200.0f);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Normal sigma", &gVulkanDenoiserSettings.normalSigma, 0.0f, 128.0f);
			ImGui::SetNextItemWidth(180.0f);
			changed |= ImGui::SliderFloat("Luma sigma", &gVulkanDenoiserSettings.lumaSigma, 0.0f, 16.0f);
			changed |= ImGui::Checkbox("Firefly clamp", &gVulkanDenoiserSettings.fireflyClamp);
			bool verboseChanged = ImGui::Checkbox("Verbose logging", &gVulkanDenoiserSettings.verboseLogging);
			changed |= verboseChanged;

			GpuStats gpuStats = runtime.vulkanPreview.gpuStats();
			ImGui::Separator();
			ImGui::Text("Mode: %s", denoiserModeLabel(gpuStats.denoiser.mode));
			ImGui::Text("View: %s", denoiserDebugViewLabel(gpuStats.denoiser.debugView));
			ImGui::Text("Strength: %.2f", gpuStats.denoiser.strength);
			ImGui::Text("Passes: %u", gpuStats.denoiser.passCount);
			if (!gpuStats.denoiser.status.empty()) {
				ImGui::TextWrapped("Status: %s", gpuStats.denoiser.status.c_str());
			}
			if (changed) {
				invalidateVulkanPostprocess(runtime);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Frame Active Scene")) {
			frameVulkanPreviewModel(runtime);
		}
		if (ImGui::MenuItem("Capture Next Vulkan Dispatch")) {
			runtime.vulkanPreview.requestRenderDocCapture();
			// A converged frame skips dispatch; clear the valid flag so
			// the next frame always renders and fires the capture.
			runtime.vulkanFrameValid = false;
		}
		ImGui::EndMenu();
	}

	if (vulkanAvailable) {
		ImGui::Separator();
		ImGui::TextDisabled("%s", firstLine(runtime.vulkanPreview.statusMessage()).c_str());
	}

	ImGui::EndMainMenuBar();
}

void drawMaterialControlLabel(const char* label, bool multipliedByTexture = false) {
	ImGui::TextUnformatted(label);
	if (multipliedByTexture) {
		ImGui::SameLine();
		ImGui::TextDisabled("(x tex)");
	}
}

bool materialSliderFloat(
	const char* label,
	const char* id,
	float* value,
	float minValue,
	float maxValue,
	bool multipliedByTexture = false,
	const char* format = "%.3f"
) {
	drawMaterialControlLabel(label, multipliedByTexture);
	ImGui::SetNextItemWidth(-1.0f);
	return ImGui::SliderFloat(id, value, minValue, maxValue, format);
}

bool materialDragFloat(
	const char* label,
	const char* id,
	float* value,
	float speed,
	float minValue,
	float maxValue,
	bool multipliedByTexture = false,
	const char* format = "%.3f"
) {
	drawMaterialControlLabel(label, multipliedByTexture);
	ImGui::SetNextItemWidth(-1.0f);
	return ImGui::DragFloat(id, value, speed, minValue, maxValue, format);
}

bool materialColorEdit3(const char* label, const char* id, float* value, bool multipliedByTexture = false) {
	drawMaterialControlLabel(label, multipliedByTexture);
	ImGui::SetNextItemWidth(-1.0f);
	return ImGui::ColorEdit3(id, value);
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
			drawMaterialControlLabel("Index");
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
			runtime.vulkanFrameValid = false;
			runtime.vulkanFrameDispatched = false;
			params.renderInvalidated = true;
		}

		bool changed = false;
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Surface");
		changed |= materialColorEdit3("Albedo", "##vulkanMaterialAlbedo", &matState.baseColor.x, matState.hasBaseColorTexture);
		changed |= materialSliderFloat("Alpha", "##vulkanMaterialAlpha", &matState.alpha, 0.0f, 1.0f);
		changed |= materialSliderFloat("Metalness", "##vulkanMaterialMetalness", &matState.metalness, 0.0f, 1.0f, matState.hasMetallicRoughnessTexture);
		changed |= materialSliderFloat("Roughness", "##vulkanMaterialRoughness", &matState.roughness, 0.0f, 1.0f, matState.hasMetallicRoughnessTexture);

		ImGui::TableSetColumnIndex(2);
		ImGui::TextUnformatted("Light/Optics");
		changed |= materialColorEdit3("Emission", "##vulkanMaterialEmission", &matState.emissiveFactor.x, matState.hasEmissiveTexture);
		changed |= materialSliderFloat("Intensity", "##vulkanMaterialEmissionIntensity", &matState.emissiveIntensity, 0.0f, 20.0f);

		ImGui::Separator();
		VulkanPreviewOpticalMode previousOpticalMode = matState.opticalMode;
		int opticalModeIndex = static_cast<int>(matState.opticalMode);
		static const char* opticalModeLabels[] = {
			"Coverage/Opaque",
			"Thin Transmission",
			"Volume Transmission"
		};
		drawMaterialControlLabel("Mode");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Combo("##vulkanMaterialOptics", &opticalModeIndex, opticalModeLabels, 3)) {
			opticalModeIndex = std::clamp(opticalModeIndex, 0, 2);
			matState.opticalMode = static_cast<VulkanPreviewOpticalMode>(opticalModeIndex);
			if (matState.opticalMode == VulkanPreviewOpticalMode::Coverage) {
				matState.transmission = 0.0f;
			}
			else if (previousOpticalMode == VulkanPreviewOpticalMode::Coverage && matState.transmission <= 0.001f) {
				matState.transmission = 1.0f;
			}
			if (matState.opticalMode == VulkanPreviewOpticalMode::VolumeTransmission && matState.volumeThickness <= 0.001f) {
				matState.volumeThickness = 0.01f;
			}
			if (matState.opticalMode == VulkanPreviewOpticalMode::VolumeTransmission && matState.attenuationDistance <= 0.001f) {
				matState.attenuationDistance = 1.0f;
			}
			changed = true;
		}
		ImGui::TextDisabled("%s", opticalModeLabel(matState.opticalMode));

		changed |= materialSliderFloat("Transmission", "##vulkanMaterialTransmission", &matState.transmission, 0.0f, 1.0f, matState.hasTransmissionTexture);
		changed |= materialSliderFloat("IOR", "##vulkanMaterialIor", &matState.ior, 1.0f, 2.5f);

		if (matState.opticalMode == VulkanPreviewOpticalMode::VolumeTransmission) {
			changed |= materialDragFloat("Thickness", "##vulkanMaterialThickness", &matState.volumeThickness, 0.001f, 0.0f, 10.0f, matState.hasThicknessTexture, "%.4f");
			changed |= materialColorEdit3("Attenuation", "##vulkanMaterialAttenuation", &matState.attenuationColor.x);
			changed |= materialDragFloat("Atten Distance", "##vulkanMaterialAttenuationDistance", &matState.attenuationDistance, 0.01f, 0.0f, 100.0f);
		}
		if (matState.hasNormalTexture) {
			changed |= materialSliderFloat("Normal Scale", "##vulkanMaterialNormalScale", &matState.normalScale, 0.0f, 4.0f);
		}

		if (changed) {
			if (runtime.vulkanPreview.setMaterialState(selectedMaterial, matState)) {
				runtime.vulkanFrameValid = false;
				runtime.vulkanFrameDispatched = false;
				params.renderInvalidated = true;
			}
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
