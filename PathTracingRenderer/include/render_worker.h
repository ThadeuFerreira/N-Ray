#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <globalParams.h>
#include <screenStartup.h>
#include <camera.h>
#include <renderer.h>

struct RenderWorkload {
	Params params;
	PTCam camera;
	Screen screen;
	RenderEnvironment environment;
	std::vector<Tri> tris;
	std::vector<PBRMaterial> materials;
	std::vector<CompactBVH> flatBVH;
};

struct RenderStatsSnapshot {
	bool active = false;
	bool complete = false;
	int sample = 0;
	unsigned long long totalRays = 0;
	double elapsedSec = 0.0;
	double raysPerSec = 0.0;
	double msPerSample = 0.0;
	unsigned long long publishedFrames = 0;
	double publishMs = 0.0;
	double samplesPerSec = 0.0;
};

struct AsyncRenderWorker {
	std::thread thread;
	std::mutex frameMutex;
	std::vector<RenderPixel> pendingFrame;
	int pendingSample = 0;
	int pendingResX = 0;
	int pendingResY = 0;
	bool frameAvailable = false;
	std::atomic<bool> running{ false };
	std::atomic<bool> cancelRequested{ false };
	std::atomic<bool> statsComplete{ false };
	std::atomic<int> statsSample{ 0 };
	std::atomic<uint64_t> statsTotalRays{ 0 };
	std::atomic<uint64_t> statsElapsedNs{ 0 };
	std::atomic<uint64_t> statsPublishedFrames{ 0 };
	std::atomic<uint64_t> statsPublishNs{ 0 };

	~AsyncRenderWorker();

	bool isRunning() const;
	void requestCancel();
	void joinFinished();
	void shutdown();
	void start(const Params& sourceParams, const Data& sourceData, const PTCam& sourceCamera, const Screen& sourceScreen, const RenderEnvironment& sourceEnvironment, const std::vector<CompactBVH>& sourceFlatBVH);
	bool consumeFrame(std::vector<RenderPixel>& frameOut, int& sampleOut, int& resXOut, int& resYOut);
	RenderStatsSnapshot stats() const;

private:
	int chooseWorkerThreadCount(const Params& workerParams) const;
	bool shouldPublishFrame(const Params& workerParams, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point lastPublish) const;
	void composeFrame(PathTracer& tracer, Params& workerParams, Data& workerData);
	void publishFrame(Data& workerData, const Params& workerParams, const Screen& workerScreen);
	void run(RenderWorkload workload);
};

void resetRenderStats(Params& params);
void applyRenderStats(Params& params, const RenderStatsSnapshot& stats);
void drawRenderTexture(Texture2D& render, const Screen& screen);
