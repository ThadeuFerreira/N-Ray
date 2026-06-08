#pragma once
#include <raylib.h>
#include <imgui.h>

inline constexpr float kUiSettingsPanelWidth = 200.0f;
inline constexpr float kUiStatsPanelWidth = 280.0f;
inline constexpr float kUiMaterialPanelHeight = 340.0f;

inline constexpr ImGuiWindowFlags kLockedPanelFlags =
	ImGuiWindowFlags_NoMove |
	ImGuiWindowFlags_NoResize |
	ImGuiWindowFlags_NoCollapse |
	ImGuiWindowFlags_NoSavedSettings;

struct UiLayout {
	Rectangle settingsPanel = {};
	Rectangle statsPanel = {};
	Rectangle viewport = {};
	Rectangle materialPanel = {};
	bool materialPanelVisible = false;
};
