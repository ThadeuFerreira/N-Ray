#include "ui.h"

void UI::drawDebugSettings(Params& params, const LayoutSizes& sizes) {
	sectionHeader("Debug Settings");

	buttonHelper("Debug Ray", "Lets cast and see a ray by clicking on the scene", sizes.button, params.enableDebugRay);

	if (buttonHelper("Material Colors", "Replaces Vulkan Pathtrace with traditional rasterization to show material unlit colors", sizes.button, params.debugMaterialColors)) {
		markRenderDirty(params);
	}
}
