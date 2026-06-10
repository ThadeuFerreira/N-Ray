#include <performance_report.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

namespace {

std::string trim(std::string s) {
	while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
		s.pop_back();
	}
	size_t start = 0;
	while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
		start++;
	}
	if (start > 0) {
		s.erase(0, start);
	}
	return s;
}

std::string shellOutput(const std::string& command) {
#if defined(_WIN32)
	FILE* pipe = _popen(command.c_str(), "r");
#else
	FILE* pipe = popen(command.c_str(), "r");
#endif
	if (pipe == nullptr) {
		return {};
	}

	std::string out;
	char buffer[256];
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
		out += buffer;
	}

#if defined(_WIN32)
	_pclose(pipe);
#else
	pclose(pipe);
#endif
	return trim(out);
}

const char* nullRedirect() {
#if defined(_WIN32)
	return " 2>NUL";
#else
	return " 2>/dev/null";
#endif
}

std::string gitOutput(const char* args) {
	std::string command = "git ";
	command += args;
	command += nullRedirect();
	return shellOutput(command);
}

std::string currentUtcIso() {
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	std::ostringstream out;
	out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return out.str();
}

std::string timestampForFile() {
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	std::ostringstream out;
	out << std::put_time(&tm, "%Y%m%d-%H%M%S");
	return out.str();
}

std::string compilerName() {
#if defined(__clang__)
	return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
	return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
	return "msvc " + std::to_string(_MSC_VER);
#else
	return "unknown";
#endif
}

std::string buildConfigName() {
#if defined(NRAY_PERFORMANCE_BUILD)
	return "Performance";
#elif defined(DEBUG)
	return "Debug";
#elif defined(NDEBUG)
	return "Release";
#else
	return "Unknown";
#endif
}

std::string platformName() {
#if defined(__linux__) || defined(__APPLE__)
	struct utsname info{};
	if (uname(&info) == 0) {
		std::ostringstream out;
		out << info.sysname << " " << info.release << " " << info.machine;
		return out.str();
	}
#endif
#if defined(_WIN32)
	return "Windows";
#elif defined(__APPLE__)
	return "macOS";
#elif defined(__linux__)
	return "Linux";
#else
	return "Unknown";
#endif
}

std::string readCpuName() {
#if defined(__linux__)
	std::ifstream f("/proc/cpuinfo");
	std::string line;
	while (std::getline(f, line)) {
		if (line.rfind("model name", 0) == 0) {
			size_t colon = line.find(':');
			if (colon != std::string::npos) {
				return trim(line.substr(colon + 1));
			}
		}
	}
#endif
	const char* envCpu = std::getenv("PROCESSOR_IDENTIFIER");
	return envCpu != nullptr ? std::string(envCpu) : std::string("unknown");
}

uint64_t readSystemMemoryBytes() {
#if defined(__linux__)
	std::ifstream f("/proc/meminfo");
	std::string key;
	uint64_t valueKb = 0;
	std::string unit;
	while (f >> key >> valueKb >> unit) {
		if (key == "MemTotal:") {
			return valueKb * 1024ull;
		}
	}
#endif
	return 0;
}

std::string jsonEscape(const std::string& s) {
	std::ostringstream out;
	for (unsigned char c : s) {
		switch (c) {
		case '"': out << "\\\""; break;
		case '\\': out << "\\\\"; break;
		case '\b': out << "\\b"; break;
		case '\f': out << "\\f"; break;
		case '\n': out << "\\n"; break;
		case '\r': out << "\\r"; break;
		case '\t': out << "\\t"; break;
		default:
			if (c < 0x20) {
				out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
			}
			else {
				out << static_cast<char>(c);
			}
			break;
		}
	}
	return out.str();
}

std::string boolJson(bool value) {
	return value ? "true" : "false";
}

std::string shadowModeName(VulkanPreviewShadowMode mode) {
	switch (mode) {
	case VulkanPreviewShadowMode::None: return "none";
	case VulkanPreviewShadowMode::RayTraced: return "ray-traced";
	case VulkanPreviewShadowMode::ShadowMap: return "shadow-map";
	default: return "unknown";
	}
}

std::string denoiserModeName(VulkanDenoiserMode mode) {
	switch (mode) {
	case VulkanDenoiserMode::Off: return "off";
	case VulkanDenoiserMode::SpatialAtrous: return "spatial-atrous";
	case VulkanDenoiserMode::SvgfLite: return "svgf-lite";
	default: return "unknown";
	}
}

std::string denoiserDebugViewName(VulkanDenoiserDebugView view) {
	switch (view) {
	case VulkanDenoiserDebugView::Final: return "final";
	case VulkanDenoiserDebugView::RawAccumulation: return "raw";
	case VulkanDenoiserDebugView::DenoisedPreview: return "denoised";
	case VulkanDenoiserDebugView::Normal: return "normal";
	case VulkanDenoiserDebugView::Albedo: return "albedo";
	case VulkanDenoiserDebugView::Depth: return "depth";
	case VulkanDenoiserDebugView::MaterialId: return "material-id";
	case VulkanDenoiserDebugView::InstanceId: return "instance-id";
	default: return "unknown";
	}
}

std::string vulkanVersionString(uint32_t packed) {
	if (packed == 0) {
		return "unknown";
	}
	uint32_t major = packed >> 22;
	uint32_t minor = (packed >> 12) & 0x3ffu;
	uint32_t patch = packed & 0xfffu;
	std::ostringstream out;
	out << major << "." << minor << "." << patch;
	return out.str();
}

std::string bytesString(uint64_t bytes) {
	if (bytes == 0) {
		return "unknown";
	}
	const char* suffixes[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double value = static_cast<double>(bytes);
	size_t suffix = 0;
	while (value >= 1024.0 && suffix + 1 < std::size(suffixes)) {
		value /= 1024.0;
		suffix++;
	}
	std::ostringstream out;
	out << std::fixed << std::setprecision(suffix == 0 ? 0 : 2) << value << " " << suffixes[suffix];
	return out.str();
}

uint32_t activeLightCount(const VulkanPreviewSettings& settings) {
	uint32_t count = 0;
	if (settings.enableSky || settings.enableEnvironment) {
		count++;
	}
	if (settings.enableSun && settings.sunIntensity > 0.0f) {
		count++;
	}
	if (settings.enableThreePointLighting) {
		if (settings.lightingDebug.keyLightEnabled && settings.keyLight.intensity > 0.0f) count++;
		if (settings.lightingDebug.fillLightEnabled && settings.fillLight.intensity > 0.0f) count++;
		if (settings.lightingDebug.rimLightEnabled && settings.rimLight.intensity > 0.0f) count++;
	}
	return count;
}

void writeStringArray(std::ostream& out, const std::vector<std::string>& values, int indent) {
	out << "[";
	if (!values.empty()) {
		out << "\n";
		for (size_t i = 0; i < values.size(); ++i) {
			out << std::string(indent, ' ') << "\"" << jsonEscape(values[i]) << "\"";
			if (i + 1 < values.size()) {
				out << ",";
			}
			out << "\n";
		}
		out << std::string(std::max(0, indent - 2), ' ');
	}
	out << "]";
}

std::filesystem::path pathWithExtension(const std::filesystem::path& prefix, const char* ext) {
	std::filesystem::path p = prefix;
	std::string current = p.extension().string();
	if (current == ".json" || current == ".md") {
		p.replace_extension(ext);
		return p;
	}
	return std::filesystem::path(prefix.string() + ext);
}

bool ensureParentDirectory(const std::filesystem::path& path, std::string* error) {
	std::filesystem::path parent = path.parent_path();
	if (parent.empty()) {
		return true;
	}
	std::error_code ec;
	std::filesystem::create_directories(parent, ec);
	if (ec) {
		if (error != nullptr) {
			*error = ec.message();
		}
		return false;
	}
	return true;
}

} // namespace

NrayPerformanceReport makeVulkanPerformanceReport(
	const VulkanComputePreview& preview,
	const VulkanPreviewSettings& settings,
	const GpuStats& stats,
	int width,
	int height,
	bool success,
	const std::string& status,
	const std::vector<std::string>& capturePaths,
	double wallTimeSecondsOverride
) {
	NrayPerformanceReport report;
	report.success = success;
	report.status = status;
	report.generatedAtUtc = currentUtcIso();

	std::string commit = gitOutput("rev-parse HEAD");
	report.engineCommit = commit.empty() ? "unknown" : commit;
	std::string branch = gitOutput("rev-parse --abbrev-ref HEAD");
	report.engineBranch = branch.empty() ? "unknown" : branch;
	std::string dirty = gitOutput("status --short --untracked-files=no");
	report.engineDirty = !dirty.empty();

	report.buildConfig = buildConfigName();
	report.compiler = compilerName();
	report.platform = platformName();
	report.cpuName = readCpuName();
	report.logicalCores = std::thread::hardware_concurrency();
	report.systemMemoryBytes = readSystemMemoryBytes();

	report.width = width;
	report.height = height;
	report.shaderIndex = preview.shaderIndex();
	const char* shader = VulkanComputePreview::shaderName(report.shaderIndex);
	report.shaderName = shader != nullptr ? shader : "Unknown";
	report.modelIndex = preview.modelIndex();
	const char* model = report.modelIndex >= 0
		? VulkanComputePreview::modelName(report.modelIndex)
		: "Default Scene";
	report.modelName = model != nullptr ? model : "Unknown";
	report.skyIndex = preview.skyIndex();
	const char* sky = VulkanComputePreview::skyName(report.skyIndex);
	report.skyName = sky != nullptr ? sky : "Procedural Sky";

	report.samplesRequested = settings.maxSamples;
	report.samplesAccumulated = static_cast<int>(stats.samplesAccumulated);
	report.converged = preview.converged();
	report.raysPerPixel = static_cast<int>(stats.raysPerPixel);
	report.maxBounces = settings.maxBounces;
	report.rrMinBounces = settings.rrMinBounces;
	report.russianRoulette = settings.russianRoulette;
	report.exposure = settings.exposure;
	report.contrast = settings.contrast;
	report.shadowMode = shadowModeName(settings.shadowMode);
	report.denoiserMode = denoiserModeName(settings.denoiser.mode);
	report.denoiserDebugView = denoiserDebugViewName(settings.denoiser.debugView);

	report.environmentEnabled = settings.enableEnvironment;
	report.skyEnabled = settings.enableSky;
	report.sunEnabled = settings.enableSun && settings.sunIntensity > 0.0f;
	report.threePointEnabled = settings.enableThreePointLighting;
	report.keyLightEnabled = settings.enableThreePointLighting && settings.lightingDebug.keyLightEnabled && settings.keyLight.intensity > 0.0f;
	report.fillLightEnabled = settings.enableThreePointLighting && settings.lightingDebug.fillLightEnabled && settings.fillLight.intensity > 0.0f;
	report.rimLightEnabled = settings.enableThreePointLighting && settings.lightingDebug.rimLightEnabled && settings.rimLight.intensity > 0.0f;
	report.pointLightShadows = settings.lightingDebug.pointLightShadows;
	report.keyLightShadows = settings.lightingDebug.keyLightShadows;
	report.fillLightShadows = settings.lightingDebug.fillLightShadows;
	report.rimLightShadows = settings.lightingDebug.rimLightShadows;
	report.directDiffuse = settings.lightingDebug.directDiffuse;
	report.directSpecular = settings.lightingDebug.directSpecular;
	report.clearcoatSpecular = settings.lightingDebug.clearcoatSpecular;
	report.keyLightIntensity = settings.keyLight.intensity;
	report.fillLightIntensity = settings.fillLight.intensity;
	report.rimLightIntensity = settings.rimLight.intensity;
	report.directSpecularScale = settings.lightingDebug.directSpecularScale;
	report.pointLightSizeScale = settings.lightingDebug.pointLightSizeScale;
	report.activeLightSources = activeLightCount(settings);

	report.gpu = stats;
	if (wallTimeSecondsOverride > 0.0) {
		report.gpu.runWallMs = wallTimeSecondsOverride * 1000.0;
	}
	report.fallbackTextures = preview.fallbackTextureLabels();
	report.captures = capturePaths;
	return report;
}

std::filesystem::path makeDefaultPerformanceReportPrefix() {
	std::filesystem::path root = std::filesystem::current_path();
	if (root.filename() == "PathTracingRenderer") {
		root = root.parent_path();
	}
	std::string commit = gitOutput("rev-parse --short=8 HEAD");
	if (commit.empty()) {
		commit = "unknown";
	}
	std::filesystem::path dir = root / "build" / "performance_reports";
	return dir / ("nray-vulkan-perf-" + timestampForFile() + "-" + commit);
}

std::filesystem::path performanceReportJsonPath(const std::filesystem::path& prefix) {
	return pathWithExtension(prefix, ".json");
}

std::filesystem::path performanceReportMarkdownPath(const std::filesystem::path& prefix) {
	return pathWithExtension(prefix, ".md");
}

bool writePerformanceReportJson(const std::filesystem::path& path, const NrayPerformanceReport& report, std::string* error) {
	if (!ensureParentDirectory(path, error)) {
		return false;
	}

	std::ofstream f(path);
	if (!f) {
		if (error != nullptr) {
			*error = "cannot open report file";
		}
		return false;
	}

	f << std::setprecision(10);
	f << "{\n";
	f << "  \"success\": " << boolJson(report.success) << ",\n";
	f << "  \"generatedAtUtc\": \"" << jsonEscape(report.generatedAtUtc) << "\",\n";
	f << "  \"renderer\": \"" << jsonEscape(report.renderer) << "\",\n";
	f << "  \"status\": \"" << jsonEscape(report.status) << "\",\n";
	f << "  \"width\": " << report.width << ",\n";
	f << "  \"height\": " << report.height << ",\n";
	f << "  \"shaderIndex\": " << report.shaderIndex << ",\n";
	f << "  \"shaderName\": \"" << jsonEscape(report.shaderName) << "\",\n";
	f << "  \"modelIndex\": " << report.modelIndex << ",\n";
	f << "  \"modelName\": \"" << jsonEscape(report.modelName) << "\",\n";
	f << "  \"skyIndex\": " << report.skyIndex << ",\n";
	f << "  \"skyName\": \"" << jsonEscape(report.skyName) << "\",\n";
	f << "  \"samplesRequested\": " << report.samplesRequested << ",\n";
	f << "  \"raysPerPixel\": " << report.raysPerPixel << ",\n";
	f << "  \"samplesAccumulated\": " << report.samplesAccumulated << ",\n";
	f << "  \"converged\": " << boolJson(report.converged) << ",\n";
	f << "  \"gpuDispatchMs\": " << report.gpu.gpuDispatchMs << ",\n";
	f << "  \"primaryRaysPerSec\": " << report.gpu.primaryRaysPerSec << ",\n";
	f << "  \"primaryRaysTraced\": " << report.gpu.primaryRaysTraced << ",\n";
	f << "  \"engine\": {"
		<< "\"commit\": \"" << jsonEscape(report.engineCommit) << "\", "
		<< "\"branch\": \"" << jsonEscape(report.engineBranch) << "\", "
		<< "\"dirty\": " << boolJson(report.engineDirty) << "},\n";
	f << "  \"machine\": {"
		<< "\"platform\": \"" << jsonEscape(report.platform) << "\", "
		<< "\"cpu\": \"" << jsonEscape(report.cpuName) << "\", "
		<< "\"logicalCores\": " << report.logicalCores << ", "
		<< "\"memoryBytes\": " << report.systemMemoryBytes << ", "
		<< "\"buildConfig\": \"" << jsonEscape(report.buildConfig) << "\", "
		<< "\"compiler\": \"" << jsonEscape(report.compiler) << "\"},\n";
	f << "  \"gpuDevice\": {"
		<< "\"name\": \"" << jsonEscape(report.gpu.deviceName) << "\", "
		<< "\"vendorId\": " << report.gpu.vendorId << ", "
		<< "\"deviceId\": " << report.gpu.deviceId << ", "
		<< "\"apiVersion\": \"" << vulkanVersionString(report.gpu.apiVersion) << "\", "
		<< "\"apiVersionRaw\": " << report.gpu.apiVersion << ", "
		<< "\"driverVersion\": " << report.gpu.driverVersion << "},\n";
	f << "  \"renderSettings\": {"
		<< "\"maxBounces\": " << report.maxBounces << ", "
		<< "\"rrMinBounces\": " << report.rrMinBounces << ", "
		<< "\"russianRoulette\": " << boolJson(report.russianRoulette) << ", "
		<< "\"exposure\": " << report.exposure << ", "
		<< "\"contrast\": " << report.contrast << ", "
		<< "\"shadowMode\": \"" << report.shadowMode << "\", "
		<< "\"denoiserMode\": \"" << report.denoiserMode << "\", "
		<< "\"denoiserDebugView\": \"" << report.denoiserDebugView << "\"},\n";
	f << "  \"timing\": {"
		<< "\"runWallMs\": " << report.gpu.runWallMs << ", "
		<< "\"lastDispatchWallMs\": " << report.gpu.dispatchWallMs << ", "
		<< "\"lastGpuDispatchMs\": " << report.gpu.gpuDispatchMs << ", "
		<< "\"totalGpuDispatchMs\": " << report.gpu.totalGpuDispatchMs << ", "
		<< "\"avgGpuDispatchMs\": " << report.gpu.avgGpuDispatchMs << ", "
		<< "\"minGpuDispatchMs\": " << report.gpu.minGpuDispatchMs << ", "
		<< "\"maxGpuDispatchMs\": " << report.gpu.maxGpuDispatchMs << ", "
		<< "\"timedDispatchCount\": " << report.gpu.timedDispatchCount << ", "
		<< "\"timestampAvailable\": " << boolJson(report.gpu.timestampAvailable) << "},\n";
	f << "  \"lighting\": {"
		<< "\"environment\": " << boolJson(report.environmentEnabled) << ", "
		<< "\"sky\": " << boolJson(report.skyEnabled) << ", "
		<< "\"sun\": " << boolJson(report.sunEnabled) << ", "
		<< "\"threePoint\": " << boolJson(report.threePointEnabled) << ", "
		<< "\"keyEnabled\": " << boolJson(report.keyLightEnabled) << ", "
		<< "\"fillEnabled\": " << boolJson(report.fillLightEnabled) << ", "
		<< "\"rimEnabled\": " << boolJson(report.rimLightEnabled) << ", "
		<< "\"activeLightSources\": " << report.activeLightSources << ", "
		<< "\"pointLightShadows\": " << boolJson(report.pointLightShadows) << ", "
		<< "\"keyLightShadows\": " << boolJson(report.keyLightShadows) << ", "
		<< "\"fillLightShadows\": " << boolJson(report.fillLightShadows) << ", "
		<< "\"rimLightShadows\": " << boolJson(report.rimLightShadows) << ", "
		<< "\"directDiffuse\": " << boolJson(report.directDiffuse) << ", "
		<< "\"directSpecular\": " << boolJson(report.directSpecular) << ", "
		<< "\"clearcoatSpecular\": " << boolJson(report.clearcoatSpecular) << ", "
		<< "\"keyIntensity\": " << report.keyLightIntensity << ", "
		<< "\"fillIntensity\": " << report.fillLightIntensity << ", "
		<< "\"rimIntensity\": " << report.rimLightIntensity << ", "
		<< "\"specularScale\": " << report.directSpecularScale << ", "
		<< "\"pointLightSize\": " << report.pointLightSizeScale << "},\n";
	f << "  \"sceneCounts\": {"
		<< "\"triangles\": " << report.gpu.sceneCounts.x << ", "
		<< "\"bvhNodes\": " << report.gpu.sceneCounts.y << ", "
		<< "\"materials\": " << report.gpu.sceneCounts.z << ", "
		<< "\"textures\": " << report.gpu.sceneCounts.w << "},\n";
	f << "  \"textureDescriptorCount\": " << report.gpu.textureDescriptorCount << ",\n";
	f << "  \"fallbackTextureCount\": " << report.gpu.fallbackTextureCount << ",\n";
	f << "  \"fallbackTextures\": ";
	writeStringArray(f, report.fallbackTextures, 4);
	f << ",\n";
	f << "  \"memory\": {"
		<< "\"localHeapBytes\": " << report.gpu.localHeapBytes << ", "
		<< "\"localHeapUsed\": " << report.gpu.localHeapUsed << ", "
		<< "\"pixelBufferBytes\": " << report.gpu.pixelBufferBytes << ", "
		<< "\"memBudgetAvailable\": " << boolJson(report.gpu.memBudgetAvailable) << "},\n";
	f << "  \"denoiser\": {"
		<< "\"enabled\": " << boolJson(report.gpu.denoiser.enabled) << ", "
		<< "\"active\": " << boolJson(report.gpu.denoiser.active) << ", "
		<< "\"strength\": " << report.gpu.denoiser.strength << ", "
		<< "\"passCount\": " << report.gpu.denoiser.passCount << ", "
		<< "\"totalMs\": " << report.gpu.denoiser.totalMs << ", "
		<< "\"status\": \"" << jsonEscape(report.gpu.denoiser.status) << "\", "
		<< "\"skipReason\": \"" << jsonEscape(report.gpu.denoiser.skipReason) << "\"},\n";
	f << "  \"captures\": ";
	writeStringArray(f, report.captures, 4);
	f << "\n";
	f << "}\n";
	return true;
}

bool writePerformanceReportMarkdown(const std::filesystem::path& path, const NrayPerformanceReport& report, std::string* error) {
	if (!ensureParentDirectory(path, error)) {
		return false;
	}

	std::ofstream f(path);
	if (!f) {
		if (error != nullptr) {
			*error = "cannot open report file";
		}
		return false;
	}

	f << "# N-Ray Vulkan Performance Report\n\n";
	f << "- Generated: " << report.generatedAtUtc << "\n";
	f << "- Success: " << (report.success ? "true" : "false") << "\n";
	f << "- Status: " << report.status << "\n";
	f << "- Engine: " << report.engineCommit << " (" << report.engineBranch << (report.engineDirty ? ", dirty" : ", clean") << ")\n";
	f << "- Build: " << report.buildConfig << " / " << report.compiler << "\n";
	f << "- Machine: " << report.platform << ", " << report.cpuName << ", " << report.logicalCores << " logical cores, " << bytesString(report.systemMemoryBytes) << " RAM\n";
	f << "- GPU: " << report.gpu.deviceName << " (vendor " << report.gpu.vendorId << ", device " << report.gpu.deviceId << ", Vulkan " << vulkanVersionString(report.gpu.apiVersion) << ")\n\n";

	f << "## Render\n\n";
	f << "- Renderer: " << report.renderer << "\n";
	f << "- Resolution: " << report.width << "x" << report.height << "\n";
	f << "- Shader: " << report.shaderName << " (" << report.shaderIndex << ")\n";
	f << "- Model: " << report.modelName << " (" << report.modelIndex << ")\n";
	f << "- Sky: " << report.skyName << " (" << report.skyIndex << ")\n";
	f << "- Samples: " << report.samplesAccumulated << " / " << report.samplesRequested << "\n";
	f << "- Rays per pixel: " << report.raysPerPixel << "\n";
	f << "- Bounces: " << report.maxBounces << "\n";
	f << "- Shadow mode: " << report.shadowMode << "\n";
	f << "- Denoiser: " << report.denoiserMode << " / " << report.denoiserDebugView << "\n\n";

	f << "## Scene\n\n";
	f << "- Triangles: " << report.gpu.sceneCounts.x << "\n";
	f << "- BVH nodes: " << report.gpu.sceneCounts.y << "\n";
	f << "- Materials: " << report.gpu.sceneCounts.z << "\n";
	f << "- Textures: " << report.gpu.sceneCounts.w << "\n";
	f << "- Texture descriptors: " << report.gpu.textureDescriptorCount << "\n";
	f << "- Fallback textures: " << report.gpu.fallbackTextureCount << "\n\n";

	f << "## Lighting\n\n";
	f << "- Active light sources: " << report.activeLightSources << "\n";
	f << "- Environment: " << (report.environmentEnabled ? "on" : "off") << "\n";
	f << "- Procedural sky: " << (report.skyEnabled ? "on" : "off") << "\n";
	f << "- Sun: " << (report.sunEnabled ? "on" : "off") << "\n";
	f << "- Key/fill/rim: " << (report.keyLightEnabled ? "on" : "off") << " / " << (report.fillLightEnabled ? "on" : "off") << " / " << (report.rimLightEnabled ? "on" : "off") << "\n";
	f << "- Shadows (master): " << (report.pointLightShadows ? "on" : "off")
	  << " (key/fill/rim: "
	  << (report.keyLightShadows ? "on" : "off") << "/"
	  << (report.fillLightShadows ? "on" : "off") << "/"
	  << (report.rimLightShadows ? "on" : "off") << ")\n\n";

	f << "## Timing\n\n";
	f << "- Total wall time: " << std::fixed << std::setprecision(3) << (report.gpu.runWallMs / 1000.0) << " sec\n";
	f << "- Last dispatch wall time: " << report.gpu.dispatchWallMs << " ms\n";
	f << "- Last GPU dispatch: " << report.gpu.gpuDispatchMs << " ms\n";
	f << "- Average GPU dispatch: " << report.gpu.avgGpuDispatchMs << " ms\n";
	f << "- Min/max GPU dispatch: " << report.gpu.minGpuDispatchMs << " / " << report.gpu.maxGpuDispatchMs << " ms\n";
	f << "- Primary rays: " << report.gpu.primaryRaysTraced << "\n";
	f << "- Primary rays/sec: " << report.gpu.primaryRaysPerSec << "\n";
	f << "- Timestamp support: " << (report.gpu.timestampAvailable ? "yes" : "no") << "\n\n";

	f << "## Memory\n\n";
	f << "- Local heap: " << bytesString(report.gpu.localHeapBytes) << "\n";
	f << "- Local heap used: " << bytesString(report.gpu.localHeapUsed) << "\n";
	f << "- Pixel buffer: " << bytesString(report.gpu.pixelBufferBytes) << "\n";
	f << "- Memory budget support: " << (report.gpu.memBudgetAvailable ? "yes" : "no") << "\n";
	return true;
}

NrayPerformanceReportPaths writePerformanceReportFiles(const std::filesystem::path& prefix, const NrayPerformanceReport& report) {
	NrayPerformanceReportPaths result;
	result.jsonPath = performanceReportJsonPath(prefix);
	result.markdownPath = performanceReportMarkdownPath(prefix);

	std::string error;
	result.jsonWritten = writePerformanceReportJson(result.jsonPath, report, &error);
	if (!result.jsonWritten && result.error.empty()) {
		result.error = "JSON: " + error;
	}

	error.clear();
	result.markdownWritten = writePerformanceReportMarkdown(result.markdownPath, report, &error);
	if (!result.markdownWritten && result.error.empty()) {
		result.error = "Markdown: " + error;
	}

	return result;
}
