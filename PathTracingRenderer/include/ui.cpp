#include "ui.h"

void UI::logic(Params& params, Data& data, PTCam& myCam) {
	LayoutSizes sizes;

	drawSettingsWindow(params, data, myCam, sizes);
	drawStatsWindow(params, data);
}

void UI::drawSettingsWindow(Params& params, Data& data, PTCam& myCam, const LayoutSizes& sizes) {
	ImGui::SetNextWindowSize(ImVec2(200.0f, float(params.screenSize.y)), ImGuiCond_Once);
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
	ImGui::Begin("Settings", nullptr);

	drawRenderSettings(params, sizes);
	drawSkySettings(params, sizes);
	drawCameraSettings(params, myCam, sizes);
	drawPostSettings(params, sizes);
	drawSceneSettings(params, data, sizes);
	drawDebugSettings(params, sizes);

	ImGui::End();
}

void UI::sectionHeader(const char* title) {
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Text("%s", title);
	ImGui::Separator();
	ImGui::Spacing();
}

void UI::markRenderDirty(Params& params) {
	params.shouldSample = false;
}

void UI::normalizeSunDirection(Params& params) {
	if (glm::length(params.sunDir) > 0.0001f) {
		params.sunDir = glm::normalize(params.sunDir);
	}
	else {
		params.sunDir = { 0.0f, 0.0f, 1.0f };
	}
}
