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

void AsyncRenderWorker::discardFrame() {
	std::lock_guard<std::mutex> lock(frameMutex);
	pendingFrame.clear();
	pendingAccum.clear();
	frameAvailable = false;
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
		sourceData.triIsect,
		sourceData.materials,
		sourceFlatBVH
	};

	{
		std::lock_guard<std::mutex> lock(frameMutex);
		pendingFrame.clear();
		pendingAccum.clear();
		pendingSample = 0;
		pendingRaysPerPixel = sourceParams.raysPerPixel;
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
	updateDisplaySettings(sourceParams, false);
	cancelRequested.store(false);
	running.store(true);
	thread = std::thread(&AsyncRenderWorker::run, this, std::move(workload));
}

void AsyncRenderWorker::updateDisplaySettings(const Params& sourceParams, bool requestRefresh) {
	displayExposure.store(sourceParams.exposure);
	displayContrast.store(sourceParams.contrast);
	displayPublishHz.store(std::max(1, sourceParams.renderPublishHz));
	if (requestRefresh) {
		displayRefreshRequested.store(true);
	}
}

bool AsyncRenderWorker::consumeFrame(std::vector<RenderPixel>& frameOut, std::vector<glm::vec3>& accumOut, int& sampleOut, int& raysPerPixelOut, int& resXOut, int& resYOut) {
	std::lock_guard<std::mutex> lock(frameMutex);

	if (!frameAvailable) {
		return false;
	}

	frameOut.swap(pendingFrame);
	accumOut.swap(pendingAccum);
	sampleOut = pendingSample;
	raysPerPixelOut = pendingRaysPerPixel;
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

bool AsyncRenderWorker::shouldPublishFrame(const Params& workerParams, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point lastPublish) {
	if (displayRefreshRequested.exchange(false)) {
		return true;
	}

	if (workerParams.currentSample <= 4 || workerParams.currentSample >= workerParams.maxSamples) {
		return true;
	}

	int publishHz = displayPublishHz.load();
	auto publishPeriod = std::chrono::duration<double>(1.0 / static_cast<double>(publishHz));
	return now - lastPublish >= publishPeriod;
}

void composeRenderFrame(PathTracer& tracer, float exposure, float contrast, int sample, int raysPerPixel, const std::vector<glm::vec3>& accumBuffer, std::vector<RenderPixel>& frameBuffer, int threadCount) {
	frameBuffer.resize(accumBuffer.size());

	float invSamples = sample > 0 ? 1.0f / (float(sample) * float(std::max(1, raysPerPixel))) : 1.0f;
	int requestedThreads = std::max(1, threadCount);

#pragma omp parallel for schedule(static) num_threads(requestedThreads)
	for (int i = 0; i < static_cast<int>(frameBuffer.size()); i++) {
		glm::vec3 col = accumBuffer[i] * invSamples;

		col *= exposure;
		col = glm::clamp(col, 0.0f, 1.0f);
		tracer.colorManagement(contrast, col);

		frameBuffer[i] = vec3ToRenderPixel(col);
	}
}

void AsyncRenderWorker::composeFrame(PathTracer& tracer, const Params& workerParams, Data& workerData, int threadCount) {
	composeRenderFrame(
		tracer,
		displayExposure.load(),
		displayContrast.load(),
		workerParams.currentSample,
		workerParams.raysPerPixel,
		workerData.accumBuffer,
		workerData.frameBuffer,
		threadCount
	);
}

void AsyncRenderWorker::publishFrame(Data& workerData, const Params& workerParams, const Screen& workerScreen) {
	std::lock_guard<std::mutex> lock(frameMutex);
	pendingFrame.swap(workerData.frameBuffer);
	pendingAccum = workerData.accumBuffer;
	pendingSample = workerParams.currentSample;
	pendingRaysPerPixel = workerParams.raysPerPixel;
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
	const int workerThreadCount = chooseWorkerThreadCount(workerParams);

	const size_t pixelCount = static_cast<size_t>(workerScreen.resX) * static_cast<size_t>(workerScreen.resY);
	const uint64_t raysPerSample = static_cast<uint64_t>(pixelCount) * static_cast<uint64_t>(workerParams.raysPerPixel);
	workerData.tris = std::move(workload.tris);
	workerData.triIsect = std::move(workload.triIsect);
	workerData.materials = std::move(workload.materials);
	workerData.frameBuffer.resize(pixelCount);
	workerData.accumBuffer.resize(pixelCount, glm::vec3(0.0f));
	std::vector<CompactBVH> flatBVH = std::move(workload.flatBVH);

	auto startTime = std::chrono::steady_clock::now();
	auto lastPublishTime = startTime;

	while (!cancelRequested.load() && workerParams.currentSample < workerParams.maxSamples) {
		for (int rays = 0; rays < workerParams.raysPerPixel && !cancelRequested.load(); rays++) {
			// Per-pixel cost varies wildly (glass/volume paths vs. background), so
			// dynamic scheduling keeps every worker thread busy instead of letting
			// one straggler with the expensive pixels stall the whole sample.
#pragma omp parallel for schedule(dynamic, 1024) num_threads(workerThreadCount)
			for (int i = 0; i < static_cast<int>(workerData.accumBuffer.size()); i++) {
				PathRay ray;
				PathRayState rayState;
				RenderRng rng = makeRenderRng(static_cast<uint32_t>(i), static_cast<uint32_t>(workerParams.currentSample), static_cast<uint32_t>(rays));
				workerTracer.generatePixelRay(static_cast<uint32_t>(i), ray, rayState, workerCamera, workerScreen, workerParams, rng);
				workerTracer.rayLogic(ray, rayState, workerData.tris, workerData.triIsect, workerData.materials, flatBVH, workerParams, workerEnvironment, rng);
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
			composeFrame(workerTracer, workerParams, workerData, workerThreadCount);
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
	params.currentSample = 0;
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
