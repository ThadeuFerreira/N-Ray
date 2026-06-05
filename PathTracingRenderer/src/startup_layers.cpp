#include <app.h>

#include <cstdint>
#include <iostream>
#include <rlImGui.h>
#include <objImporter.h>

void createFlatBVH() {
	globalBVH.clear();

	if (data.tris.empty()) {
		return;
	}

	globalBVH.reserve(data.tris.size() * 2);
	globalBVH.emplace_back();
	globalBVH[0] = BVH(0, static_cast<uint32_t>(data.tris.size() - 1), data.tris, globalBVH);
}

void loadSceneLayer() {
	std::cout << "Loading Scene..." << '\n';
	ObjImporter scene{ "models/scene.obj", data,
		{0.7f, 0.7f, 0.7f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},
		1.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };

	ObjImporter glass{ "models/sceneGlass.obj", data,
		{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f},{1.0f, 1.0f, 1.0f},{1.0f, 1.0f, 1.0f},
		1.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 15.0f, 0.0f , true };

	ObjImporter metal{ "models/sceneMetal.obj", data,
		{0.9f, 0.9f, 0.9f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f},{1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},
		1.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, true };

	ObjImporter red{ "models/sceneRed.obj", data,
		{0.7f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f},{1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},
		1.5f, 0.15f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, true };

	ObjImporter dragon{ "models/dragon.obj", data,
		{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f},{0.5f, 0.6f, 0.0f},{1.0f, 1.0f, 1.0f},
		1.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 15.0f, 0.0f , true };

	std::cout << "Creating Lights..." << '\n';
}

void initializeRenderLayer() {
	std::cout << "Initializing Window..." << '\n';
	screen.initScreen(params.res, data.frameBuffer, data.accumBuffer);

	std::cout << "Build BVH Tree..." << '\n';
	createFlatBVH();
	if (!globalBVH.empty()) {
		pt.flattenBVH(0, globalBVH, globalCompactBVH);
	}

	for (size_t i = 0; i < data.tris.size(); i++) {
		data.tris[i].idx = uint32_t(i);
		data.models[data.tris[i].modelIdx].tris.push_back(uint32_t(i));
	}

	// Build the cache-friendly intersection mirror once geometry is final and
	// indices are assigned. BVH traversal reads this instead of the fat Tri.
	data.triIsect.resize(data.tris.size());
	for (size_t i = 0; i < data.tris.size(); i++) {
		const Tri& t = data.tris[i];
		data.triIsect[i] = { t.a, t.eA, t.eB, t.idx, t.doubleSided ? 1u : 0u };
	}

	std::cout << data.models.size() << '\n';

	cam3D.position = { myCam.camPos.x, myCam.camPos.y, myCam.camPos.z };
	cam3D.target = { myCam.camTarget.x, myCam.camTarget.y, myCam.camTarget.z };
	cam3D.up = { myCam.up.x, myCam.up.y, myCam.up.z };
	cam3D.fovy = myCam.fov;
	cam3D.projection = CAMERA_PERSPECTIVE;
}

void startupRuntimeLayer(RuntimeResources& runtime) {
	runtime.prevRes = params.res;

	runtime.render = createRenderTexture();
	runtime.vulkanPreview.initialize(screen.resX, screen.resY);
	std::cout << runtime.vulkanPreview.statusMessage() << '\n';

	runtime.hdri = LoadImage("textures/HDRI.hdr");
	if (runtime.hdri.data == nullptr) {
		std::cerr << "Warning: failed to load textures/HDRI.hdr (missing file, or raylib "
			"built without HDR support). Falling back to procedural sky.\n";
	}
	else {
		ImageFormat(&runtime.hdri, PIXELFORMAT_UNCOMPRESSED_R32G32B32);
	}

	rlImGuiSetup(true);
}

void shutdownRuntimeLayer(RuntimeResources& runtime) {
	runtime.renderWorker.shutdown();
	runtime.vulkanPreview.shutdown();
	rlImGuiShutdown();
	UnloadTexture(runtime.render);
	UnloadImage(runtime.hdri);
}
