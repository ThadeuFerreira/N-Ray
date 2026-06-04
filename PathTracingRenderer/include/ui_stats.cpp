#include "ui.h"

float UI::updateAverageUiFrameMs(float currentMs) {
	avgMsFrames[avgMsIdx] = currentMs;

	float totalMs = 0.0f;
	for (size_t i = 0; i < avgMsIdxAmount; i++) {
		totalMs += avgMsFrames[i];
	}

	avgMsIdx++;
	if (avgMsIdx >= avgMsIdxAmount) {
		avgMsIdx = 0;
	}

	return totalMs / float(avgMsIdxAmount);
}

void UI::drawStatsWindow(Params& params, Data& data) {
	float uiFrameMs = params.dt * 1000.0f;
	float avgUiFrameMs = updateAverageUiFrameMs(uiFrameMs);

	ImGui::SetNextWindowSize(ImVec2(250.0f, 400.0f), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(params.screenSize.x - 250.0f, 0.0f), ImGuiCond_Once);
	ImGui::Begin("Stats", nullptr);

	const char* renderStatus = "Idle";
	if (params.renderStatsActive) {
		renderStatus = "Rendering";
	}
	else if (params.renderStatsComplete) {
		renderStatus = "Complete";
	}
	else if (params.render) {
		renderStatus = "Waiting";
	}

	double totalRaysM = static_cast<double>(params.renderStatsTotalRays) / 1000000.0;
	double raysPerSecM = params.renderStatsRaysPerSec / 1000000.0;
	float sampleProgress = params.maxSamples > 0 ? static_cast<float>(params.currentSample) / static_cast<float>(params.maxSamples) : 0.0f;
	sampleProgress = glm::clamp(sampleProgress, 0.0f, 1.0f);

	ImGui::Text("UI FPS: %d", GetFPS());
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("UI frame: %.2f ms", uiFrameMs);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Avg. UI frame: %.2f ms", avgUiFrameMs);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Triangles: %d", (int)data.tris.size());
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Render: %s", renderStatus);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Samples: %d / %d", params.currentSample, params.maxSamples);
	ImGui::ProgressBar(sampleProgress, ImVec2(-1.0f, 0.0f));
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Worker samples: %d", params.renderStatsSample);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Rays traced: %.2f M", totalRaysM);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Rays/sec: %.2f M", raysPerSecM);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Samples/sec: %.2f", params.renderStatsSamplesPerSec);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Render time: %.2f sec", params.renderStatsElapsedSec);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Avg sample: %.2f ms", params.renderStatsMsPerSample);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Published frames: %llu", params.renderStatsPublishedFrames);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Avg publish: %.2f ms", params.renderStatsPublishMs);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Bounces: %d", params.maxBounces);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::End();
}
