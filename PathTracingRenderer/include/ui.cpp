#include "ui.h"

void UI::logic(Params& params, Data& data, PTCam& myCam, const UiLayout& layout) {
	LayoutSizes sizes;

	drawSettingsWindow(params, data, myCam, sizes, layout);
	drawStatsWindow(params, data, layout);
}

void UI::drawSettingsWindow(Params& params, Data& data, PTCam& myCam, const LayoutSizes& sizes, const UiLayout& layout) {
	ImGui::SetNextWindowSize(ImVec2(layout.settingsPanel.width, layout.settingsPanel.height), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(layout.settingsPanel.x, layout.settingsPanel.y), ImGuiCond_Always);
	ImGui::Begin("Settings", nullptr, kLockedPanelFlags);

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
	params.renderInvalidated = true;
	params.displayInvalidated = true;
	params.shouldSample = false;
}

void UI::markDisplayDirty(Params& params) {
	params.displayInvalidated = true;
}

void UI::normalizeSunDirection(Params& params) {
	if (glm::length(params.sunDir) > 0.0001f) {
		params.sunDir = glm::normalize(params.sunDir);
	}
	else {
		params.sunDir = { 0.0f, 0.0f, 1.0f };
	}
}
