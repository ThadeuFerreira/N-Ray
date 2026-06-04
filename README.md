# Introduction

## N-Ray is a path tracing renderer made for learning purposes by Narcis Calin

<img width="1907" height="1049" alt="nraygit1" src="https://github.com/user-attachments/assets/47c8204e-2707-4daa-8590-5df1636e7441" />

<img width="1908" height="1047" alt="nraygit2" src="https://github.com/user-attachments/assets/a736a0f1-e4ab-4317-9970-99c941f2b077" />

<img width="1908" height="1049" alt="nraygit3" src="https://github.com/user-attachments/assets/a1c402a2-148b-4d69-abaa-65863e2f12b6" />

<img width="640" height="720" alt="Git1GifPt" src="https://github.com/user-attachments/assets/b879ecbe-55df-41e9-986f-6e30aa0848f7" />


<img width="640" height="720" alt="Git2GifPt" src="https://github.com/user-attachments/assets/6b48117d-5eae-4c6b-95a2-d44bd4a28e84" />

## Developer Guide & Testing

### Prerequisites

- **C++ Compiler**: A compiler supporting C++20 (Visual Studio 2022, GCC 11+, or Clang 13+).
- **Raylib**: The project depends on Raylib. On Linux/macOS, ensure it is installed or accessible via your library path.
- **Premake5**: Used for cross-platform project generation.
- **OpenMP**: Required for multi-threaded path tracing.

### Building the Project

#### Windows (Visual Studio)
1. Open `PathTracingRenderer.sln` in Visual Studio 2022.
2. Select the `x64` platform.
3. Choose `Release` or `Debug` configuration.
4. Press `F5` to build and run.

#### Cross-Platform (Premake5)
1. Run `premake5 [action]` where `[action]` is `vs2022`, `gmake2`, or `xcode4`.
2. For example, on Linux/macOS:
   ```bash
   make
   ```
   This will generate Makefiles in the `build` directory and compile the project. The executable will be placed in `bin/Release` or `bin/Debug`.

### Manual Testing & Controls

Once the renderer is running, you can use the following controls to test its functionality:

#### Camera Movement
- **W / A / S / D**: Move forward, left, backward, and right.
- **Left Ctrl**: Move downward.
- **Left Shift**: Move upward.
- **Right Mouse Button (Hold)**: Rotate the camera to look around.

#### Interaction
- **Left Mouse Button**: 
  - Click on the scene to trace a **Debug Ray** (visualized as a cylinder showing the ray path).
  - Click on a model to **Select** it (useful for checking material assignments).
  - If "Click DoF" is enabled in settings, click on an object to set the **Focal Distance**.

#### UI (ImGui)
- **Settings Panel**: Adjust rendering resolution, max bounces, samples per pixel, and environment lighting (HDRI).
- **Stats Panel**: Monitor frames per second (FPS) and accumulation progress.
- **Material Editing**: When a model is selected, its material properties (color, roughness, emission, etc.) can be modified in real-time.

### Testing Workflow
1. **Load Scene**: The project loads `models/scene.obj` by default. Ensure the `models` folder exists in the working directory.
2. **Verify Rendering**: Move the camera; the accumulation should reset and restart.
3. **Debug Rays**: Use LMB to verify ray-triangle intersections and bounce logic.
4. **Performance**: Check the "Stats" panel to ensure the renderer is utilizing OpenMP effectively (all CPU cores should show high usage during rendering).
