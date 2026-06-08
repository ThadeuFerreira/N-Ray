// NrayRenderDocHeadless — drives VulkanComputePreview from the command line.
// No raylib, no ImGui, no global app state.

#include <vulkan_compute_preview.h>
#include <globalParams.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// RenderDoc API for querying saved capture paths.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif
#include "../../vendor/slang/external/renderdoc_app.h"

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct HeadlessOptions {
	int width          = 512;
	int height         = 512;
	int shaderIndex    = -1;  // -1 = auto (model preview if model loaded)
	int modelIndex     = -1;  // index into project_settings.json manifest
	std::string modelFolder;  // transient folder, NOT persisted
	int samples        = 1;
	int maxBounces     = 5;
	int rrMinBounces   = 3;
	VulkanPreviewShadowMode shadowMode   = VulkanPreviewShadowMode::RayTraced;
	VulkanDenoiserMode denoiserMode      = VulkanDenoiserMode::Off;
	VulkanDenoiserDebugView debugView    = VulkanDenoiserDebugView::Final;
	bool doCapture         = false;
	std::string captureTemplate;
	std::string jsonOut;
	std::string ppmOut;
	bool helpRequested = false;
};

static void printHelp(const char* argv0) {
	std::cout <<
		"Usage: " << argv0 << " [options]\n"
		"\n"
		"Options:\n"
		"  --width <pixels>           render width (default 512)\n"
		"  --height <pixels>          render height (default 512)\n"
		"  --shader-index <index>     compute shader index (default: model preview)\n"
		"  --model-index <index>      select model from project_settings.json manifest\n"
		"  --model-folder <path>      transient glTF/GLB folder (not persisted)\n"
		"  --samples <count>          samples to accumulate (default 1)\n"
		"  --max-bounces <count>      max path-trace bounces (default 5)\n"
		"  --shadow <none|ray-traced|shadow-map>  shadow mode (default ray-traced)\n"
		"  --denoiser <off|spatial-atrous>        denoiser (default off)\n"
		"  --debug-view <final|raw|denoised|normal|albedo|depth|material-id|instance-id>\n"
		"  --capture                  arm RenderDoc capture for next dispatch\n"
		"  --capture-template <path>  RenderDoc capture path template\n"
		"  --json-out <path>          write run metadata as JSON\n"
		"  --ppm-out <path>           write output pixels as PPM image\n"
		"  --help                     show this help\n"
		"\n"
		"To capture with RenderDoc, launch via renderdoccmd:\n"
		"  renderdoccmd capture --wait-for-exit \\\n"
		"    --working-dir /path/to/N-Ray/PathTracingRenderer \\\n"
		"    --capture-file /path/to/build/renderdoc/nray_headless \\\n"
		"    NrayRenderDocHeadless --model-index 0 --samples 1 --capture\n";
}

static HeadlessOptions parseArgs(int argc, char** argv) {
	HeadlessOptions opts;

	auto nextArg = [&](int& i, const char* flag) -> const char* {
		if (i + 1 >= argc) {
			std::cerr << "error: " << flag << " requires an argument\n";
			std::exit(1);
		}
		return argv[++i];
	};

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
			opts.helpRequested = true;
		} else if (std::strcmp(a, "--width") == 0) {
			opts.width = std::atoi(nextArg(i, "--width"));
		} else if (std::strcmp(a, "--height") == 0) {
			opts.height = std::atoi(nextArg(i, "--height"));
		} else if (std::strcmp(a, "--shader-index") == 0) {
			opts.shaderIndex = std::atoi(nextArg(i, "--shader-index"));
		} else if (std::strcmp(a, "--model-index") == 0) {
			opts.modelIndex = std::atoi(nextArg(i, "--model-index"));
		} else if (std::strcmp(a, "--model-folder") == 0) {
			opts.modelFolder = nextArg(i, "--model-folder");
		} else if (std::strcmp(a, "--samples") == 0) {
			opts.samples = std::atoi(nextArg(i, "--samples"));
		} else if (std::strcmp(a, "--max-bounces") == 0) {
			opts.maxBounces = std::atoi(nextArg(i, "--max-bounces"));
		} else if (std::strcmp(a, "--shadow") == 0) {
			const char* v = nextArg(i, "--shadow");
			if (std::strcmp(v, "none") == 0)
				opts.shadowMode = VulkanPreviewShadowMode::None;
			else if (std::strcmp(v, "shadow-map") == 0)
				opts.shadowMode = VulkanPreviewShadowMode::ShadowMap;
			else
				opts.shadowMode = VulkanPreviewShadowMode::RayTraced;
		} else if (std::strcmp(a, "--denoiser") == 0) {
			const char* v = nextArg(i, "--denoiser");
			if (std::strcmp(v, "spatial-atrous") == 0)
				opts.denoiserMode = VulkanDenoiserMode::SpatialAtrous;
			else
				opts.denoiserMode = VulkanDenoiserMode::Off;
		} else if (std::strcmp(a, "--debug-view") == 0) {
			const char* v = nextArg(i, "--debug-view");
			if      (std::strcmp(v, "raw") == 0)         opts.debugView = VulkanDenoiserDebugView::RawAccumulation;
			else if (std::strcmp(v, "denoised") == 0)    opts.debugView = VulkanDenoiserDebugView::DenoisedPreview;
			else if (std::strcmp(v, "normal") == 0)      opts.debugView = VulkanDenoiserDebugView::Normal;
			else if (std::strcmp(v, "albedo") == 0)      opts.debugView = VulkanDenoiserDebugView::Albedo;
			else if (std::strcmp(v, "depth") == 0)       opts.debugView = VulkanDenoiserDebugView::Depth;
			else if (std::strcmp(v, "material-id") == 0) opts.debugView = VulkanDenoiserDebugView::MaterialId;
			else if (std::strcmp(v, "instance-id") == 0) opts.debugView = VulkanDenoiserDebugView::InstanceId;
			else                                         opts.debugView = VulkanDenoiserDebugView::Final;
		} else if (std::strcmp(a, "--capture") == 0) {
			opts.doCapture = true;
		} else if (std::strcmp(a, "--capture-template") == 0) {
			opts.captureTemplate = nextArg(i, "--capture-template");
		} else if (std::strcmp(a, "--json-out") == 0) {
			opts.jsonOut = nextArg(i, "--json-out");
		} else if (std::strcmp(a, "--ppm-out") == 0) {
			opts.ppmOut = nextArg(i, "--ppm-out");
		} else {
			std::cerr << "warning: unknown option: " << a << "\n";
		}
	}

	opts.samples = std::clamp(opts.samples, kMinSamples, kMaxSamples);
	if (opts.width  < 1) opts.width  = 1;
	if (opts.height < 1) opts.height = 1;
	return opts;
}

// ---------------------------------------------------------------------------
// Camera computation — mirrors frameVulkanPreviewModel() in app_loop.cpp
// ---------------------------------------------------------------------------

static VulkanPreviewCamera computeCamera(
	const glm::vec3& boundsMin,
	const glm::vec3& boundsMax,
	float fovDeg,
	float aspect)
{
	glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
	glm::vec3 extent = boundsMax - boundsMin;

	float radius = glm::length(extent) * 0.5f;
	if (!std::isfinite(radius) || radius <= 0.0001f) {
		radius = 1.0f;
	}

	float halfFov  = glm::radians(fovDeg) * 0.5f;
	float fovScale = std::max(std::tan(halfFov), 0.1f);
	float distance = (radius / fovScale) * 1.45f;

	glm::vec3 position = center + glm::vec3(0.0f, -distance, radius * 0.35f);
	glm::vec3 forward  = glm::normalize(center - position);
	glm::vec3 worldUp  = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::vec3 right    = glm::normalize(glm::cross(forward, worldUp));
	glm::vec3 up       = glm::normalize(glm::cross(right, forward));

	// verticalScale = tan(halfFov): the camera half-extent in world space per unit depth
	VulkanPreviewCamera cam;
	cam.position      = position;
	cam.forward       = forward;
	cam.right         = right;
	cam.up            = up;
	cam.verticalScale = fovScale;
	cam.aspect        = aspect;
	return cam;
}

static VulkanPreviewCamera defaultCamera(float aspect) {
	VulkanPreviewCamera cam;
	cam.position      = glm::vec3(0.0f, -3.0f, 1.5f);
	cam.forward       = glm::normalize(glm::vec3(0.0f, 3.0f, -1.5f));
	cam.right         = glm::vec3(1.0f, 0.0f, 0.0f);
	cam.up            = glm::vec3(0.0f, 0.0f, 1.0f);
	cam.verticalScale = std::tan(glm::radians(22.5f));
	cam.aspect        = aspect;
	return cam;
}

// ---------------------------------------------------------------------------
// Settings builder
// ---------------------------------------------------------------------------

static VulkanPreviewSettings buildSettings(const HeadlessOptions& opts) {
	VulkanPreviewSettings s;
	s.maxSamples      = opts.samples;
	s.maxBounces      = opts.maxBounces;
	s.rrMinBounces    = opts.rrMinBounces;
	s.russianRoulette = true;
	s.exposure        = 1.0f;
	s.contrast        = 0.8f;
	s.skyIntensity    = 0.75f;
	s.enableSky       = true;
	s.enableSun       = false;
	s.shadowMode      = opts.shadowMode;
	s.denoiser.mode   = opts.denoiserMode;
	s.denoiser.debugView = opts.debugView;
	s.resetAccumulation = false;
	s.postprocessOnly   = false;
	return s;
}

// ---------------------------------------------------------------------------
// PPM writer
// ---------------------------------------------------------------------------

static bool savePpm(const std::string& path,
	const std::vector<RenderPixel>& pixels, int w, int h)
{
	std::ofstream f(path, std::ios::binary);
	if (!f) {
		std::cerr << "error: cannot write PPM to " << path << "\n";
		return false;
	}
	f << "P6\n" << w << " " << h << "\n255\n";
	for (const RenderPixel& px : pixels) {
		f.put(static_cast<char>(px.r));
		f.put(static_cast<char>(px.g));
		f.put(static_cast<char>(px.b));
	}
	return true;
}

// ---------------------------------------------------------------------------
// JSON metadata writer
// ---------------------------------------------------------------------------

static void escapeJson(std::ostream& out, const std::string& s) {
	for (char c : s) {
		if      (c == '"')  out << "\\\"";
		else if (c == '\\') out << "\\\\";
		else if (c == '\n') out << "\\n";
		else                out << c;
	}
}

static bool saveJson(const std::string& path,
	const HeadlessOptions& opts,
	const VulkanComputePreview& preview,
	const std::vector<std::string>& capturePaths,
	bool success)
{
	std::ofstream f(path);
	if (!f) {
		std::cerr << "error: cannot write JSON to " << path << "\n";
		return false;
	}

	GpuStats stats = preview.gpuStats();
	const char* shaderName = VulkanComputePreview::shaderName(preview.shaderIndex());
	const char* modelName  = VulkanComputePreview::modelName(preview.modelIndex());

	f << "{\n";
	f << "  \"success\": " << (success ? "true" : "false") << ",\n";
	f << "  \"width\": "  << opts.width  << ",\n";
	f << "  \"height\": " << opts.height << ",\n";
	f << "  \"shaderIndex\": " << preview.shaderIndex() << ",\n";
	f << "  \"shaderName\": \""; escapeJson(f, shaderName ? shaderName : ""); f << "\",\n";
	f << "  \"modelIndex\": " << preview.modelIndex() << ",\n";
	f << "  \"modelName\": \""; escapeJson(f, modelName ? modelName : ""); f << "\",\n";
	f << "  \"samplesRequested\": " << opts.samples << ",\n";
	f << "  \"samplesAccumulated\": " << preview.samplesAccumulated() << ",\n";
	f << "  \"converged\": " << (preview.converged() ? "true" : "false") << ",\n";
	f << "  \"gpuDispatchMs\": " << stats.gpuDispatchMs << ",\n";
	f << "  \"primaryRaysPerSec\": " << stats.primaryRaysPerSec << ",\n";
	f << "  \"status\": \""; escapeJson(f, preview.statusMessage()); f << "\",\n";
	f << "  \"captures\": [";
	for (size_t i = 0; i < capturePaths.size(); ++i) {
		if (i > 0) f << ", ";
		f << "\""; escapeJson(f, capturePaths[i]); f << "\"";
	}
	f << "]\n";
	f << "}\n";
	return true;
}

// ---------------------------------------------------------------------------
// RenderDoc capture path query
// ---------------------------------------------------------------------------

static std::vector<std::string> queryCapturePaths(uint32_t capturesBefore) {
#ifdef _WIN32
	HMODULE mod = GetModuleHandleA("renderdoc.dll");
	pRENDERDOC_GetAPI getApi = mod
		? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"))
		: nullptr;
#elif defined(__linux__)
	auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI"));
#else
	pRENDERDOC_GetAPI getApi = nullptr;
#endif

	if (getApi == nullptr) return {};
	void* apiPtr = nullptr;
	if (getApi(eRENDERDOC_API_Version_1_4_1, &apiPtr) != 1 || apiPtr == nullptr) return {};
	auto* api = reinterpret_cast<RENDERDOC_API_1_4_1*>(apiPtr);

	std::vector<std::string> paths;
	uint32_t total = api->GetNumCaptures ? api->GetNumCaptures() : 0u;
	for (uint32_t i = capturesBefore; i < total; ++i) {
		if (api->GetCapture == nullptr) break;
		uint32_t len = 0;
		api->GetCapture(i, nullptr, &len, nullptr);
		if (len == 0) continue;
		std::string buf(len, '\0');
		api->GetCapture(i, buf.data(), &len, nullptr);
		// Strip the null terminator that RenderDoc includes in the length.
		while (!buf.empty() && buf.back() == '\0') buf.pop_back();
		paths.push_back(buf);
	}
	return paths;
}

static uint32_t currentCaptureCount() {
#ifdef _WIN32
	HMODULE mod = GetModuleHandleA("renderdoc.dll");
	pRENDERDOC_GetAPI getApi = mod
		? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"))
		: nullptr;
#elif defined(__linux__)
	auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI"));
#else
	pRENDERDOC_GetAPI getApi = nullptr;
#endif

	if (getApi == nullptr) return 0;
	void* apiPtr = nullptr;
	if (getApi(eRENDERDOC_API_Version_1_4_1, &apiPtr) != 1 || apiPtr == nullptr) return 0;
	auto* api = reinterpret_cast<RENDERDOC_API_1_4_1*>(apiPtr);
	return api->GetNumCaptures ? api->GetNumCaptures() : 0u;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
	HeadlessOptions opts = parseArgs(argc, argv);
	if (opts.helpRequested) {
		printHelp(argv[0]);
		return 0;
	}

	std::cout << "[nray-headless] initializing Vulkan " << opts.width << "x" << opts.height << "\n";

	VulkanComputePreview preview;
	preview.setPersistSettings(false);
	if (!preview.initialize(opts.width, opts.height)) {
		std::cerr << "[nray-headless] Vulkan init failed: " << preview.statusMessage() << "\n";
		if (!opts.jsonOut.empty()) {
			saveJson(opts.jsonOut, opts, preview, {}, false);
		}
		return 1;
	}
	std::cout << "[nray-headless] Vulkan ready: " << preview.statusMessage() << "\n";

	// --- Load model ---
	bool hasModel = false;
	if (!opts.modelFolder.empty()) {
		std::cout << "[nray-headless] importing transient model: " << opts.modelFolder << "\n";
		int idx = preview.importModelFromFolder(opts.modelFolder, /*persist=*/false);
		if (idx < 0) {
			std::cerr << "[nray-headless] model import failed: " << preview.statusMessage() << "\n";
			if (!opts.jsonOut.empty()) { saveJson(opts.jsonOut, opts, preview, {}, false); }
			preview.shutdown();
			return 1;
		}
		if (!preview.setModel(idx)) {
			std::cerr << "[nray-headless] setModel(" << idx << ") failed after import: "
				<< preview.statusMessage() << "\n";
			if (!opts.jsonOut.empty()) { saveJson(opts.jsonOut, opts, preview, {}, false); }
			preview.shutdown();
			return 1;
		}
		hasModel = true;
		std::cout << "[nray-headless] model loaded: "
			<< (VulkanComputePreview::modelName(idx) ? VulkanComputePreview::modelName(idx) : "?")
			<< " (index " << idx << ")\n";
	} else if (opts.modelIndex >= 0) {
		std::cout << "[nray-headless] selecting manifest model index " << opts.modelIndex << "\n";
		if (preview.setModel(opts.modelIndex)) {
			hasModel = true;
			std::cout << "[nray-headless] model loaded: "
				<< (VulkanComputePreview::modelName(opts.modelIndex)
					? VulkanComputePreview::modelName(opts.modelIndex) : "?")
				<< "\n";
		} else {
			std::cerr << "[nray-headless] setModel(" << opts.modelIndex
				<< ") failed: " << preview.statusMessage() << "\n";
			if (!opts.jsonOut.empty()) { saveJson(opts.jsonOut, opts, preview, {}, false); }
			preview.shutdown();
			return 1;
		}
	} else if (VulkanComputePreview::modelCount() > 0) {
		// No explicit model requested but the manifest has entries — auto-select the
		// first one so the default capture covers glTF scene resources, not a tutorial
		// shader.
		const char* modelName = VulkanComputePreview::modelName(0);
		std::cout << "[nray-headless] no model specified; auto-selecting manifest[0]: "
			<< (modelName ? modelName : "?") << "\n";
		if (!preview.setModel(0)) {
			std::cerr << "[nray-headless] auto-select manifest[0] failed"
				<< (modelName ? std::string(" (") + modelName + ")" : std::string())
				<< ": " << preview.statusMessage() << "\n";
			if (!opts.jsonOut.empty()) { saveJson(opts.jsonOut, opts, preview, {}, false); }
			preview.shutdown();
			return 1;
		}
		hasModel = true;
		std::cout << "[nray-headless] model loaded: "
			<< (modelName ? modelName : "?") << " (index 0)\n";
	}

	// --- Select shader ---
	int shaderIdx = opts.shaderIndex;
	if (shaderIdx < 0) {
		// Auto: model preview if a model loaded, otherwise first shader.
		if (hasModel) {
			int mp = VulkanComputePreview::modelPreviewShaderIndex();
			shaderIdx = mp >= 0 ? mp : 0;
		} else {
			shaderIdx = 0;
		}
	}
	if (!preview.setShader(shaderIdx)) {
		std::cerr << "[nray-headless] setShader(" << shaderIdx << ") failed: "
			<< preview.statusMessage() << "\n";
		if (!opts.jsonOut.empty()) { saveJson(opts.jsonOut, opts, preview, {}, false); }
		preview.shutdown();
		return 1;
	}
	std::cout << "[nray-headless] shader: "
		<< (VulkanComputePreview::shaderName(shaderIdx) ? VulkanComputePreview::shaderName(shaderIdx) : "?")
		<< " (index " << shaderIdx << ")\n";

	// --- Build camera ---
	float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);
	VulkanPreviewCamera camera;
	glm::vec3 bmin, bmax;
	if (hasModel && preview.modelBounds(bmin, bmax)) {
		camera = computeCamera(bmin, bmax, 45.0f, aspect);
		std::cout << "[nray-headless] camera framed from model bounds\n";
	} else {
		camera = defaultCamera(aspect);
		std::cout << "[nray-headless] using default camera\n";
	}

	// --- Build settings ---
	VulkanPreviewSettings settings = buildSettings(opts);

	// --- Arm capture ---
	uint32_t capturesBefore = currentCaptureCount();
	if (opts.doCapture) {
		if (!opts.captureTemplate.empty()) {
			if (preview.setCaptureTemplate(opts.captureTemplate)) {
				std::cout << "[nray-headless] RenderDoc capture template: " << opts.captureTemplate << "\n";
			} else {
				std::cerr << "[nray-headless] RenderDoc capture template not applied (RenderDoc not injected?)\n";
			}
		}
		if (preview.requestRenderDocCapture()) {
			std::cout << "[nray-headless] RenderDoc capture armed\n";
		} else {
			std::cerr << "[nray-headless] " << preview.statusMessage() << "\n";
		}
	}

	// --- Render loop ---
	std::cout << "[nray-headless] rendering " << opts.samples << " sample(s)...\n";
	std::vector<RenderPixel> pixels(static_cast<size_t>(opts.width) * opts.height);
	auto t0 = std::chrono::steady_clock::now();
	float timeSeconds = 0.0f;
	bool renderOk = false;

	while (true) {
		renderOk = preview.render(timeSeconds, camera, settings, pixels);
		if (!renderOk) {
			std::cerr << "[nray-headless] render() failed: " << preview.statusMessage() << "\n";
			break;
		}
		settings.resetAccumulation = false;
		if (preview.converged()) {
			break;
		}
		auto now = std::chrono::steady_clock::now();
		timeSeconds = std::chrono::duration<float>(now - t0).count();
	}

	if (renderOk) {
		std::cout << "[nray-headless] done: " << preview.samplesAccumulated()
			<< "/" << opts.samples << " samples\n";
	}

	// --- Collect capture paths ---
	std::vector<std::string> capturePaths = queryCapturePaths(capturesBefore);
	if (!capturePaths.empty()) {
		for (const std::string& p : capturePaths) {
			std::cout << "[nray-headless] capture saved: " << p << "\n";
		}
	}

	// --- Save outputs ---
	if (!opts.ppmOut.empty() && renderOk) {
		if (savePpm(opts.ppmOut, pixels, opts.width, opts.height)) {
			std::cout << "[nray-headless] PPM written: " << opts.ppmOut << "\n";
		}
	}

	if (!opts.jsonOut.empty()) {
		if (saveJson(opts.jsonOut, opts, preview, capturePaths, renderOk)) {
			std::cout << "[nray-headless] JSON written: " << opts.jsonOut << "\n";
		}
	}

	preview.shutdown();
	return renderOk ? 0 : 1;
}
