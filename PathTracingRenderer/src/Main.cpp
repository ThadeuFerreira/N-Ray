#define _CRT_SECURE_NO_WARNINGS

#include <app.h>

int main() {
	configureApplication();
	startupWindow();

	loadSceneLayer();
	initializeRenderLayer();

	RuntimeResources runtime;
	startupRuntimeLayer(runtime);

	runMainLoop(runtime);

	shutdownRuntimeLayer(runtime);
	shutdownWindow();

	return 0;
}
