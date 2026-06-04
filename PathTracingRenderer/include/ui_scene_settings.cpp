#include "ui.h"

UI::SelectedMaterialState UI::collectSelectedMaterialState(const Data& data) const {
	SelectedMaterialState state;

	for (const PTModel& model : data.models) {
		if (!model.selected) {
			continue;
		}

		state.albedo += model.albedo;
		state.specularCol += model.specularCol;
		state.emissionCol += model.emissionCol;
		state.absorptionCol += model.absorptionCol;
		state.volumeCol += model.volumeCol;
		state.IOR += model.IOR;
		state.roughness += model.roughness;
		state.emissionIntensity += model.emissionIntensity;
		state.refraction += model.refraction;
		state.absorption += model.absorption;
		state.volume += model.volume;
		state.density += model.density;
		state.metalness += model.metalness;
		state.selectedCount++;
	}

	normalizeSelectedMaterialState(state);
	return state;
}

void UI::normalizeSelectedMaterialState(SelectedMaterialState& state) {
	if (state.selectedCount == 0) {
		return;
	}

	float inv = 1.0f / float(state.selectedCount);
	state.albedo *= inv;
	state.specularCol *= inv;
	state.emissionCol *= inv;
	state.absorptionCol *= inv;
	state.volumeCol *= inv;
	state.IOR *= inv;
	state.roughness *= inv;
	state.emissionIntensity *= inv;
	state.refraction *= inv;
	state.absorption *= inv;
	state.volume *= inv;
	state.density *= inv;
	state.metalness *= inv;
}

void UI::applySelectedMaterialState(Data& data, const SelectedMaterialState& state) {
	for (PTModel& model : data.models) {
		if (!model.selected) {
			continue;
		}

		model.albedo = state.albedo;
		model.specularCol = state.specularCol;
		model.emissionCol = state.emissionCol;
		model.absorptionCol = state.absorptionCol;
		model.volumeCol = state.volumeCol;
		model.IOR = state.IOR;
		model.roughness = state.roughness;
		model.emissionIntensity = state.emissionIntensity;
		model.refraction = state.refraction;
		model.absorption = state.absorption;
		model.volume = state.volume;
		model.density = state.density;
		model.metalness = state.metalness;
		model.updateTris(data);
	}
}

void UI::drawSceneSettings(Params& params, Data& data, const LayoutSizes& sizes) {
	sectionHeader("Scene Settings");

	buttonHelper("Enable Selection", "Allows selecting models", sizes.button, params.enableSelection);

	SelectedMaterialState state = collectSelectedMaterialState(data);
	bool materialChanged = false;

	materialChanged |= ImGui::ColorEdit3("Albedo Color", (float*)&state.albedo, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	materialChanged |= ImGui::ColorEdit3("Specular Color", (float*)&state.specularCol, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	materialChanged |= ImGui::ColorEdit3("Emission Color", (float*)&state.emissionCol, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	materialChanged |= ImGui::ColorEdit3("Absorption Color", (float*)&state.absorptionCol, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
	materialChanged |= ImGui::ColorEdit3("Volume Color", (float*)&state.volumeCol, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);

	materialChanged |= sliderHelper("IOR", "Index of Refraction of selected models", sizes.slider, state.IOR, 0.0f, 200.0f, LogSlider);
	materialChanged |= sliderHelper("Roughness", "Roughness of selected models", sizes.slider, state.roughness, 0.0f, 1.0f);
	materialChanged |= sliderHelper("Emission Intensity", "Emission intensity of selected models", sizes.slider, state.emissionIntensity, 0.0f, 100.0f, LogSlider);
	materialChanged |= sliderHelper("Refraction", "Refraction of selected models", sizes.slider, state.refraction, 0.0f, 1.0f);
	materialChanged |= sliderHelper("Absorption", "How much light a material absorbs when refracted", sizes.slider, state.absorption, 0.0f, 10.0f);
	materialChanged |= sliderHelper("Volume", "Volume scattering of selected models", sizes.slider, state.volume, 0.0f, 1.0f);
	materialChanged |= sliderHelper("Density", "Volume density of selected models", sizes.slider, state.density, 0.0f, 100.0f);
	materialChanged |= sliderHelper("Metalness", "Metalness of selected models", sizes.slider, state.metalness, 0.0f, 1.0f);

	if (materialChanged) {
		markRenderDirty(params);
		applySelectedMaterialState(data, state);
	}
}
