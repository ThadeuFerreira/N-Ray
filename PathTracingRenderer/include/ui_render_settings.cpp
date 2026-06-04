#include "ui.h"

void UI::drawRenderSettings(Params& params, const LayoutSizes& sizes) {
	sectionHeader("Render Settings");

	if (buttonHelper("Render", "Renders scene with path tracing", sizes.button, params.render)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Bounces Amount", "Amount of times a ray can bounce", sizes.slider, params.maxBounces, 0, 50)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Max Samples", "Max amount of samples to render", sizes.slider, params.maxSamples, 1, 50000)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Rays Per Pixel", "Amount of rays each pixel traces per sample", sizes.slider, params.raysPerPixel, 1, 8)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Worker Threads", "CPU render threads. 0 reserves one hardware thread for the UI", sizes.slider, params.renderWorkerThreads, 0, 64)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Publish Hz", "Maximum viewport update rate while tracing", sizes.slider, params.renderPublishHz, 1, 120)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Resolution", "Sets image resolution size", sizes.slider, params.res, 16, 1024)) {
		markRenderDirty(params);
	}
}

void UI::drawPostSettings(Params& params, const LayoutSizes& sizes) {
	sectionHeader("Post Settings");

	if (sliderHelper("Exposure", "Controls image exposure after rendering", sizes.slider, params.exposure, 0.0f, 5.0f, LogSlider)) {
		markRenderDirty(params);
	}

	if (sliderHelper("Contrast", "Controls image contrast", sizes.slider, params.contrast, 0.0f, 2.0f, LogSlider)) {
		markRenderDirty(params);
	}
}
