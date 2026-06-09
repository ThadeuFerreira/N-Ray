#include "ui.h"

void UI::drawSkySettings(Params& params, const LayoutSizes& sizes) {
	sectionHeader("Sky Settings");

	if (sliderHelper("Sun Dir X", "Sets sun direction X", sizes.slider, params.sunDir.x, 0.0f, 1.0f)) {
		markRenderDirty(params);
		normalizeSunDirection(params);
	}

	if (sliderHelper("Sun Dir Y", "Sets sun direction Y", sizes.slider, params.sunDir.y, 0.0f, 1.0f)) {
		markRenderDirty(params);
		normalizeSunDirection(params);
	}

	if (sliderHelper("Sun Dir Z", "Sets sun direction Z", sizes.slider, params.sunDir.z, 0.0f, 1.0f)) {
		markRenderDirty(params);
		normalizeSunDirection(params);
	}

	if (sliderHelper("Sun Angle", "Sets sun size in degrees", sizes.slider, params.sunAngle, 0.0f, 90.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Sun Intensity", "Sets sun intensity", sizes.slider, params.sunIntensity, 0.0f, 100.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Sky Intensity", "Sets sky intensity", sizes.slider, params.skyIntensity, 0.0f, 10.0f, LogSlider)) {
		markRenderDirty(params);
	}

	ImGui::Spacing();
	ImGui::Separator();

	if (ImGui::ColorEdit3("Sun Color", (float*)&params.sunColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) {
		markRenderDirty(params);
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		params.sunColor = glm::vec3(1.0f);
		markRenderDirty(params);
	}

	ImGui::Spacing();
	ImGui::Separator();

	if (buttonHelper("Enable Sun", "Enables sun", sizes.button, params.enableSun)) {
		markRenderDirty(params);
	}

	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Separator();

	if (buttonHelper("Enable Sky", "Enables sky", sizes.button, params.enableSky)) {
		markRenderDirty(params);
	}

	if (buttonHelper("Enable Environment", "Use selected HDRI/EXR sky for ray misses and reflections", sizes.button, params.enableEnvironment)) {
		markRenderDirty(params);
	}

	ImGui::Spacing();
	ImGui::Separator();

	sectionHeader("Three Point Lighting");

	if (buttonHelper("Enable 3-Point Lighting", "Enables analytic key/fill/rim lights (Vulkan preview)", sizes.button, params.enableThreePointLighting)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Key Intensity", "Main light intensity", sizes.slider, params.keyLightIntensity, 0.0f, 64.0f, LogSlider)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Fill Intensity", "Fill light intensity (softens shadows)", sizes.slider, params.fillLightIntensity, 0.0f, 64.0f, LogSlider)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Rim Intensity", "Rim/back light intensity (edge highlight)", sizes.slider, params.rimLightIntensity, 0.0f, 64.0f, LogSlider)) {
		markRenderDirty(params);
	}

	ImGui::Spacing();
	ImGui::Separator();
}
