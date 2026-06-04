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
	std::vector<TriIntersect> triIsect;
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
	std::vector<glm::vec3> pendingAccum;
	int pendingSample = 0;
	int pendingRaysPerPixel = 1;
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
	std::atomic<float> displayExposure{ 1.0f };
	std::atomic<float> displayContrast{ 0.8f };
	std::atomic<int> displayPublishHz{ 30 };
	std::atomic<bool> displayRefreshRequested{ false };

	~AsyncRenderWorker();

	bool isRunning() const;
	void requestCancel();
	void joinFinished();
	void shutdown();
	void discardFrame();
	void start(const Params& sourceParams, const Data& sourceData, const PTCam& sourceCamera, const Screen& sourceScreen, const RenderEnvironment& sourceEnvironment, const std::vector<CompactBVH>& sourceFlatBVH);
	void updateDisplaySettings(const Params& sourceParams, bool requestRefresh);
	bool consumeFrame(std::vector<RenderPixel>& frameOut, std::vector<glm::vec3>& accumOut, int& sampleOut, int& raysPerPixelOut, int& resXOut, int& resYOut);
	RenderStatsSnapshot stats() const;

private:
	int chooseWorkerThreadCount(const Params& workerParams) const;
	bool shouldPublishFrame(const Params& workerParams, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point lastPublish);
	void composeFrame(PathTracer& tracer, const Params& workerParams, Data& workerData, int threadCount);
	void publishFrame(Data& workerData, const Params& workerParams, const Screen& workerScreen);
	void run(RenderWorkload workload);
};

void composeRenderFrame(PathTracer& tracer, float exposure, float contrast, int sample, int raysPerPixel, const std::vector<glm::vec3>& accumBuffer, std::vector<RenderPixel>& frameBuffer, int threadCount = 1);
void resetRenderStats(Params& params);
void applyRenderStats(Params& params, const RenderStatsSnapshot& stats);
void drawRenderTexture(Texture2D& render, const Screen& screen);
