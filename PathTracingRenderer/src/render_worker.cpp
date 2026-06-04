#include <render_worker.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>
#include <omp.h>

AsyncRenderWorker::~AsyncRenderWorker() {
	shutdown();
}

bool AsyncRenderWorker::isRunning() const {
	return running.load();
}

void AsyncRenderWorker::requestCancel() {
	cancelRequested.store(true);
}

void AsyncRenderWorker::joinFinished() {
	if (!running.load() && thread.joinable()) {
		thread.join();
	}
}

void AsyncRenderWorker::shutdown() {
	requestCancel();
	if (thread.joinable()) {
		thread.join();
	}
}

void AsyncRenderWorker::start(const Params& sourceParams, const Data& sourceData, const PTCam& sourceCamera, const Screen& sourceScreen, const RenderEnvironment& sourceEnvironment, const std::vector<CompactBVH>& sourceFlatBVH) {
	joinFinished();

	if (running.load()) {
		requestCancel();
		return;
	}

	RenderWorkload workload{
		sourceParams,
		sourceCamera,
		sourceScreen,
		sourceEnvironment,
		sourceData.tris,
		sourceData.materials,
		sourceFlatBVH
	};

	{
		std::lock_guard<std::mutex> lock(frameMutex);
		pendingFrame.clear();
		pendingSample = 0;
		pendingResX = sourceScreen.resX;
		pendingResY = sourceScreen.resY;
		frameAvailable = false;
	}

	statsComplete.store(false);
	statsSample.store(0);
	statsTotalRays.store(0);
	statsElapsedNs.store(0);
	statsPublishedFrames.store(0);
	statsPublishNs.store(0);
	cancelRequested.store(false);
	running.store(true);
	thread = std::thread(&AsyncRenderWorker::run, this, std::move(workload));
}

bool AsyncRenderWorker::consumeFrame(std::vector<RenderPixel>& frameOut, int& sampleOut, int& resXOut, int& resYOut) {
	std::lock_guard<std::mutex> lock(frameMutex);

	if (!frameAvailable) {
		return false;
	}

	frameOut.swap(pendingFrame);
	sampleOut = pendingSample;
	resXOut = pendingResX;
	resYOut = pendingResY;
	frameAvailable = false;
	return true;
}

RenderStatsSnapshot AsyncRenderWorker::stats() const {
	RenderStatsSnapshot snapshot;
	snapshot.active = running.load();
	snapshot.complete = statsComplete.load();
	snapshot.sample = statsSample.load();
	snapshot.totalRays = statsTotalRays.load();

	uint64_t elapsedNs = statsElapsedNs.load();
	snapshot.elapsedSec = static_cast<double>(elapsedNs) / 1000000000.0;
	if (snapshot.elapsedSec > 0.0) {
		snapshot.raysPerSec = static_cast<double>(snapshot.totalRays) / snapshot.elapsedSec;
	}
	if (snapshot.sample > 0) {
		snapshot.msPerSample = (snapshot.elapsedSec * 1000.0) / static_cast<double>(snapshot.sample);
	}
	snapshot.publishedFrames = statsPublishedFrames.load();
	uint64_t publishNs = statsPublishNs.load();
	snapshot.publishMs = snapshot.publishedFrames > 0
		? (static_cast<double>(publishNs) / 1000000.0) / static_cast<double>(snapshot.publishedFrames)
		: 0.0;
	snapshot.samplesPerSec = snapshot.elapsedSec > 0.0
		? static_cast<double>(snapshot.sample) / snapshot.elapsedSec
		: 0.0;

	return snapshot;
}

int AsyncRenderWorker::chooseWorkerThreadCount(const Params& workerParams) const {
	if (workerParams.renderWorkerThreads > 0) {
		return std::max(1, workerParams.renderWorkerThreads);
	}

	unsigned int hardwareThreads = std::thread::hardware_concurrency();
	if (hardwareThreads <= 1) {
		return 1;
	}

	return std::max(1, static_cast<int>(hardwareThreads) - 1);
}

bool AsyncRenderWorker::shouldPublishFrame(const Params& workerParams, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point lastPublish) const {
	if (workerParams.currentSample <= 4 || workerParams.currentSample >= workerParams.maxSamples) {
		return true;
	}

	int publishHz = std::max(1, workerParams.renderPublishHz);
	auto publishPeriod = std::chrono::duration<double>(1.0 / static_cast<double>(publishHz));
	return now - lastPublish >= publishPeriod;
}

void AsyncRenderWorker::composeFrame(PathTracer& tracer, Params& workerParams, Data& workerData) {
	float invSamples = workerParams.currentSample > 0 ? 1.0f / (float(workerParams.currentSample) * float(workerParams.raysPerPixel)) : 1.0f;

#pragma omp parallel for schedule(static)
	for (int i = 0; i < static_cast<int>(workerData.frameBuffer.size()); i++) {
		glm::vec3 col = workerData.accumBuffer[i] * invSamples;

		col *= workerParams.exposure;
		col = glm::min(col, 1.0f);
		tracer.colorManagement(workerParams.contrast, col);

		workerData.frameBuffer[i] = vec3ToRenderPixel(col);
	}
}

void AsyncRenderWorker::publishFrame(Data& workerData, const Params& workerParams, const Screen& workerScreen) {
	std::lock_guard<std::mutex> lock(frameMutex);
	pendingFrame.swap(workerData.frameBuffer);
	pendingSample = workerParams.currentSample;
	pendingResX = workerScreen.resX;
	pendingResY = workerScreen.resY;
	frameAvailable = true;
}

void AsyncRenderWorker::run(RenderWorkload workload) {
	PathTracer workerTracer;
	Data workerData;
	Params workerParams = workload.params;
	PTCam workerCamera = workload.camera;
	Screen workerScreen = workload.screen;
	RenderEnvironment workerEnvironment = workload.environment;

	workerParams.currentSample = 0;
	workerParams.shouldSample = true;
	omp_set_num_threads(chooseWorkerThreadCount(workerParams));

	const size_t pixelCount = static_cast<size_t>(workerScreen.resX) * static_cast<size_t>(workerScreen.resY);
	const uint64_t raysPerSample = static_cast<uint64_t>(pixelCount) * static_cast<uint64_t>(workerParams.raysPerPixel);
	workerData.tris = std::move(workload.tris);
	workerData.materials = std::move(workload.materials);
	workerData.frameBuffer.resize(pixelCount);
	workerData.accumBuffer.resize(pixelCount, glm::vec3(0.0f));
	std::vector<CompactBVH> flatBVH = std::move(workload.flatBVH);

	auto startTime = std::chrono::steady_clock::now();
	auto lastPublishTime = startTime;

	while (!cancelRequested.load() && workerParams.currentSample < workerParams.maxSamples) {
		for (int rays = 0; rays < workerParams.raysPerPixel && !cancelRequested.load(); rays++) {
#pragma omp parallel for schedule(static)
			for (int i = 0; i < static_cast<int>(workerData.accumBuffer.size()); i++) {
				PathRay ray;
				PathRayState rayState;
				RenderRng rng = makeRenderRng(static_cast<uint32_t>(i), static_cast<uint32_t>(workerParams.currentSample), static_cast<uint32_t>(rays));
				workerTracer.generatePixelRay(static_cast<uint32_t>(i), ray, rayState, workerCamera, workerScreen, workerParams, rng);
				workerTracer.rayLogic(ray, rayState, workerData.tris, workerData.materials, flatBVH, workerParams, workerEnvironment, rng);
				workerData.accumBuffer[i] += rayState.col;
			}
		}

		if (cancelRequested.load()) {
			break;
		}

		workerParams.currentSample++;

		auto now = std::chrono::steady_clock::now();
		uint64_t elapsedNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - startTime).count());
		statsSample.store(workerParams.currentSample);
		statsTotalRays.store(static_cast<uint64_t>(workerParams.currentSample) * raysPerSample);
		statsElapsedNs.store(elapsedNs);

		if (!cancelRequested.load() && shouldPublishFrame(workerParams, now, lastPublishTime)) {
			auto publishStart = std::chrono::steady_clock::now();
			composeFrame(workerTracer, workerParams, workerData);
			publishFrame(workerData, workerParams, workerScreen);
			workerData.frameBuffer.resize(pixelCount);
			auto publishEnd = std::chrono::steady_clock::now();
			uint64_t publishNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(publishEnd - publishStart).count());
			statsPublishNs.fetch_add(publishNs);
			statsPublishedFrames.fetch_add(1);
			lastPublishTime = publishEnd;
		}
	}

	statsComplete.store(!cancelRequested.load() && workerParams.currentSample >= workerParams.maxSamples);
	running.store(false);
}

void resetRenderStats(Params& params) {
	params.renderStatsActive = false;
	params.renderStatsComplete = false;
	params.renderStatsSample = 0;
	params.renderStatsTotalRays = 0;
	params.renderStatsElapsedSec = 0.0;
	params.renderStatsRaysPerSec = 0.0;
	params.renderStatsMsPerSample = 0.0;
	params.renderStatsPublishedFrames = 0;
	params.renderStatsPublishMs = 0.0;
	params.renderStatsSamplesPerSec = 0.0;
}

void applyRenderStats(Params& params, const RenderStatsSnapshot& stats) {
	params.renderStatsActive = stats.active;
	params.renderStatsComplete = stats.complete;
	params.renderStatsSample = stats.sample;
	params.renderStatsTotalRays = stats.totalRays;
	params.renderStatsElapsedSec = stats.elapsedSec;
	params.renderStatsRaysPerSec = stats.raysPerSec;
	params.renderStatsMsPerSample = stats.msPerSample;
	params.renderStatsPublishedFrames = stats.publishedFrames;
	params.renderStatsPublishMs = stats.publishMs;
	params.renderStatsSamplesPerSec = stats.samplesPerSec;
	params.currentSample = stats.sample;
}

void drawRenderTexture(Texture2D& render, const Screen& screen) {
	DrawTexturePro(
		render,
		{ 0, 0, (float)screen.resX, (float)screen.resY },
		{ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() },
		{ 0, 0 },
		0.0f,
		WHITE
	);
}
