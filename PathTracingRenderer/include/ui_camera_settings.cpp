#include "ui.h"

void UI::drawCameraSettings(Params& params, PTCam& myCam, const LayoutSizes& sizes) {
	sectionHeader("Camera Settings");

	if (sliderHelper("Antialiasing Blur", "Sets how blurry the image renders", sizes.slider, params.blur, 0.0f, 10.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Sensor Size", "Sets sensor size in mm.", sizes.slider, myCam.sensorSize, 1.0f, 200.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Focal Length", "Sets focal length in mm.", sizes.slider, myCam.focalLengthMM, 1.0f, 1000.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Aperture", "Controls camera aperture for DOF", sizes.slider, myCam.aperture, 0.0f, 10.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Focus Distance", "How far from the camera is the focus point", sizes.slider, myCam.focusDist, 0.0f, 100.0f)) {
		markRenderDirty(params);
	}

	if (sliderHelper("ISO", "ISO for camera exposure", sizes.slider, myCam.ISO, 0.0f, 5.0f)) {
		markRenderDirty(params);
	}

	ImGui::Spacing();
	ImGui::Separator();

	buttonHelper("Pick DOF", "Lets you set a focus point by clicking the scene", sizes.button, myCam.clickDof);

	ImGui::Spacing();
	ImGui::Separator();
}
