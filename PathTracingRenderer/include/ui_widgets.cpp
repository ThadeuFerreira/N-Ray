#include "ui.h"

namespace {
ImVec2 resolveWidgetSize(glm::vec2 size) {
	if (size.x > 0.0f && size.y > 0.0f) {
		return ImVec2(size.x, size.y);
	}
	if (size.x < 0.0f && size.y > 0.0f) {
		return ImVec2(ImGui::GetContentRegionAvail().x, size.y);
	}
	if (size.x > 0.0f && size.y < 0.0f) {
		return ImVec2(size.x, ImGui::GetContentRegionAvail().y);
	}
	return ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
}

// Shared body for every slider overload: label, default-value capture for the
// right-click reset, tooltip, and enable/disable. The per-type ImGui call is
// supplied by the caller as `drawSlider`.
template <typename T, typename DrawSlider>
bool sliderImpl(const std::string& label, const std::string& tooltip, T& parameter, bool isEnabled, DrawSlider drawSlider) {
	bool isSliderUsed = false;

	ImGuiID sliderId = ImGui::GetID(label.c_str());
	static std::unordered_map<ImGuiID, T> defaultValues;

	if (!isEnabled) {
		ImGui::BeginDisabled();
	}

	if (defaultValues.find(sliderId) == defaultValues.end()) {
		defaultValues[sliderId] = parameter;
	}

	ImGui::Text("%s", label.c_str());

	if (drawSlider(("##" + label).c_str(), parameter)) {
		isSliderUsed = true;
	}

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", tooltip.c_str());
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		parameter = defaultValues[sliderId];
		isSliderUsed = true;
	}

	if (!isEnabled) {
		ImGui::EndDisabled();
	}

	return isSliderUsed;
}
}

bool UI::sliderHelper(std::string label, std::string tooltip, glm::vec2 size, float& parameter, float minVal, float maxVal,
	bool isEnabled) {
	(void)size;
	return sliderImpl(label, tooltip, parameter, isEnabled,
		[&](const char* id, float& p) { return ImGui::SliderFloat(id, &p, minVal, maxVal, "%.3f"); });
}

bool UI::sliderHelper(std::string label, std::string tooltip, glm::vec2 size, float& parameter, float minVal, float maxVal, int logarithmic,
	bool isEnabled) {
	(void)size;
	(void)logarithmic;
	return sliderImpl(label, tooltip, parameter, isEnabled,
		[&](const char* id, float& p) { return ImGui::SliderFloat(id, &p, minVal, maxVal, "%.3f", ImGuiSliderFlags_Logarithmic); });
}

bool UI::sliderHelper(std::string label, std::string tooltip, glm::vec2 size, int& parameter, int minVal, int maxVal,
	bool isEnabled) {
	(void)size;
	return sliderImpl(label, tooltip, parameter, isEnabled,
		[&](const char* id, int& p) { return ImGui::SliderInt(id, &p, minVal, maxVal); });
}

bool UI::buttonHelper(std::string label, std::string tooltip, glm::vec2 size, bool& parameter, bool canDeactivateSelf, bool isEnabled) {
	if (!isEnabled) {
		ImGui::BeginDisabled();
	}

	bool hasBeenPressed = false;
	ImVec2 buttonSize = resolveWidgetSize(size);
	bool pushedColors = false;

	if (parameter) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
		pushedColors = true;
	}

	if (ImGui::Button(label.c_str(), buttonSize)) {
		if (canDeactivateSelf) {
			parameter = !parameter;
		}
		else if (!parameter) {
			parameter = true;
		}

		hasBeenPressed = true;
	}

	if (pushedColors) {
		ImGui::PopStyleColor(3);
	}

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", tooltip.c_str());
	}

	if (!isEnabled) {
		ImGui::EndDisabled();
	}

	return hasBeenPressed;
}
