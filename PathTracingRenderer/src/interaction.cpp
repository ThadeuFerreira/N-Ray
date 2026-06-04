#include <app.h>

#include <algorithm>
#include <cfloat>

namespace {
std::vector<DebugRay> debugRays;
float debugRaySpeed = 50.0f;
}

void mousePosDisplay() {
	Vector2 mousePos = GetMousePosition();

	DrawText(TextFormat("X: %d", int(mousePos.x)), int(mousePos.x) - 50, int(mousePos.y) - 40, 20, DARKGRAY);
	DrawText(TextFormat("Y: %d", int(mousePos.y)), int(mousePos.x) - 50, int(mousePos.y) - 20, 20, DARKGRAY);
}

void traceDebugRay(const RenderEnvironment& environment) {
	if (IsMouseButtonPressed(0) && !params.isMouseHoveringUI && !myCam.clickDof) {
		PathRay mRay = mRayGen.mouseRay(params, data, screen, pt, myCam);
		PathRayState mRayState = mRayGen.mouseRayState();
		RenderRng rng = makeRenderRng(0, static_cast<uint32_t>(params.currentSample), 0);

		debugRays = pt.rayLogic(mRay, mRayState, data.tris, data.materials, globalCompactBVH, params, environment, rng, true);
	}

	for (size_t i = 0; i < debugRays.size(); i++) {
		DebugRay& r = debugRays[i];

		RenderPixel pixel = vec3ToRenderPixel(r.col);
		Color finalCol{ pixel.r, pixel.g, pixel.b, pixel.a };

		r.progress += GetFrameTime() * debugRaySpeed;

		if (i > 0 && debugRays[i - 1].progress < debugRays[i - 1].length) {
			r.progress = 0.0f;
		}

		r.progress = std::min(r.progress, r.length);

		glm::vec3 length = r.dir * r.progress;
		glm::vec3 endPos = r.src + length;

		DrawCylinderEx({ r.src.x, r.src.y, r.src.z }, { endPos.x, endPos.y, endPos.z }, 0.01f, 0.01f, 12, finalCol);
	}
}

void setDofDist() {
	PathRay dofRay = mRayGen.mouseRay(params, data, screen, pt, myCam);
	PathRayState dofRayState = mRayGen.mouseRayState();

	float closestT = FLT_MAX;
	dofRayState.hit = false;
	dofRayState.triIdx = UINT32_MAX;

	pt.traverseFlatBVH(dofRay, dofRayState, closestT, data.tris, globalCompactBVH);

	if (!dofRayState.hit) {
		myCam.focusDist = closestT;
	}
	else {
		myCam.focusDist = glm::distance(myCam.camPos, dofRayState.hitPos);
	}
}

void selectModel() {
	if (IsMouseButtonPressed(0) && !params.isMouseHoveringUI) {
		PathRay selecRay = mRayGen.mouseRay(params, data, screen, pt, myCam);
		PathRayState selecRayState = mRayGen.mouseRayState();

		float closestT = FLT_MAX;
		selecRayState.hit = false;
		selecRayState.triIdx = UINT32_MAX;

		pt.traverseFlatBVH(selecRay, selecRayState, closestT, data.tris, globalCompactBVH);

		for (size_t i = 0; i < data.models.size(); i++) {
			data.models[i].selected = false;
		}

		if (selecRayState.hit) {
			data.models[data.tris[selecRayState.triIdx].modelIdx].selected = true;;
		}
	}
}
