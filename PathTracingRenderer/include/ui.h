#pragma once
#include <imgui.h>
#include <rlImGui.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <glm.hpp>
#include <globalParams.h>
#include <camera.h>
#include <pbr_model.h>
#include <ui_layout.h>

struct UI {

	void logic(Params& params, Data& data, PTCam& myCam, const UiLayout& layout);

private:
	struct LayoutSizes {
		glm::vec2 slider = { 200.0f, 30.0f };
		glm::vec2 button = { 150.0f, 30.0f };
	};

	struct SelectedMaterialState {
		glm::vec3 albedo = { 0.0f, 0.0f, 0.0f };
		glm::vec3 specularCol = { 0.0f, 0.0f, 0.0f };
		glm::vec3 emissionCol = { 0.0f, 0.0f, 0.0f };
		glm::vec3 absorptionCol = { 0.0f, 0.0f, 0.0f };
		glm::vec3 volumeCol = { 0.0f, 0.0f, 0.0f };
		float IOR = 0.0f;
		float roughness = 0.0f;
		float emissionIntensity = 0.0f;
		float refraction = 0.0f;
		float absorption = 0.0f;
		float volume = 0.0f;
		float density = 0.0f;
		float metalness = 0.0f;
		uint32_t selectedCount = 0;
	};

	int avgMsIdx = 0;
	static const int avgMsIdxAmount = 50;
	float avgMsFrames[avgMsIdxAmount] = { 0.0f };

	enum ExtraParams {
		LogSlider
	};

	void drawSettingsWindow(Params& params, Data& data, PTCam& myCam, const LayoutSizes& sizes, const UiLayout& layout);
	void drawRenderSettings(Params& params, const LayoutSizes& sizes);
	void drawSkySettings(Params& params, const LayoutSizes& sizes);
	void drawCameraSettings(Params& params, PTCam& myCam, const LayoutSizes& sizes);
	void drawPostSettings(Params& params, const LayoutSizes& sizes);
	void drawSceneSettings(Params& params, Data& data, const LayoutSizes& sizes);
	void drawDebugSettings(Params& params, const LayoutSizes& sizes);
	void drawStatsWindow(Params& params, Data& data, const UiLayout& layout);

	float updateAverageUiFrameMs(float currentMs);

	static void sectionHeader(const char* title);
	static void markRenderDirty(Params& params);
	static void markDisplayDirty(Params& params);
	static void normalizeSunDirection(Params& params);

	SelectedMaterialState collectSelectedMaterialState(const Data& data) const;
	static void normalizeSelectedMaterialState(SelectedMaterialState& state);
	static void applySelectedMaterialState(Data& data, const SelectedMaterialState& state);

	static bool sliderHelper(std::string label, std::string tooltip, glm::vec2 size, float& parameter, float minVal, float maxVal, 
		int logarithmic, bool isEnabled = true);

	static bool sliderHelper(std::string label, std::string tooltip, glm::vec2 size, float& parameter, float minVal, float maxVal,
		bool isEnabled = true);

	// Int Overload
	static bool sliderHelper(std::string label, std::string tooltip, glm::vec2 size, int& parameter, int minVal, int maxVal,
		bool isEnabled = true);

	static bool buttonHelper(std::string label, std::string tooltip, glm::vec2 size, bool& parameter, bool canDeactivateSelf = true,
		bool isEnabled = true);
};
