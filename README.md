# N-Ray

N-Ray is a learning-focused path tracing renderer by Narcis Calin. This fork is actively moving the renderer toward a Vulkan compute path tracer while retaining the CPU path tracer as a reference/fallback. The app uses raylib for windowing/input/texture display, Dear ImGui through rlImGui for tools, GLM for math, and OpenMP for CPU parallelism.

<img width="1907" height="1049" alt="nraygit1" src="https://github.com/user-attachments/assets/47c8204e-2707-4daa-8590-5df1636e7441" />

<img width="1908" height="1047" alt="nraygit2" src="https://github.com/user-attachments/assets/a736a0f1-e4ab-4317-9970-99c941f2b077" />

<img width="1908" height="1049" alt="nraygit3" src="https://github.com/user-attachments/assets/a1c402a2-148b-4d69-abaa-65863e2f12b6" />

<img width="640" height="720" alt="Git1GifPt" src="https://github.com/user-attachments/assets/b879ecbe-55df-41e9-986f-6e30aa0848f7" />

<img width="640" height="720" alt="Git2GifPt" src="https://github.com/user-attachments/assets/6b48117d-5eae-4c6b-95a2-d44bd4a28e84" />

## Dependencies

Use submodules when cloning:

```bash
git clone --recurse-submodules https://github.com/ThadeuFerreira/N-Ray.git
cd N-Ray
```

For an existing checkout:

```bash
git submodule update --init --recursive
```

Vendored dependencies live under `vendor/`:

- `raylib`: active window/input/display backend.
- `volk`, `VulkanMemoryAllocator`, `glm`, `slang`: Vulkan-port dependencies, staged for future GPU work.

The Linux Makefile builds `vendor/raylib/src/libraylib.a` from source and patches raylib HDR support on (`SUPPORT_FILEFORMAT_HDR 1`) before compiling it.

## Build With Makefile

The root `Makefile` wraps raylib build, Premake generation, and the generated Makefiles. It is the simplest path on Linux.

```bash
make                    # Release build
make CONFIG=debug_x64   # Debug build
make build-performance  # Performance build: O3/native/fast-math/LTO where supported
make run                # Build Release and run from PathTracingRenderer/
make run-performance    # Build and run bin/Performance/PathTracingRenderer
make raylib             # Rebuild only vendor/raylib/src/libraylib.a
make generate           # Regenerate build/ project files
make clean              # Remove build/, bin/, obj/
make distclean          # Also clean raylib objects/library
```

Run commands intentionally use `PathTracingRenderer/` as the working directory because assets are loaded from relative paths such as `models/scene.obj` and `textures/HDRI.hdr`.

## Validation Assets

The top-level `assets/` directory is the local glTF validation corpus for Vulkan importer and PBR material work. Use `assets/*/scene.gltf` or `.glb` bundles as the first source of validation models before downloading external samples. Keep each bundle's scene file, binary buffers, textures, and license/source files together in its subfolder.

Current glTF entry points include:

- `assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf`
- `assets/2024_lbsilhouette_works_murcielago_gt_evo/scene.gltf`
- `assets/accurate_torvosaurus_tanneri/scene.gltf`
- `assets/beretta_arx160/scene.gltf`
- `assets/beretta_m9_gameready/scene.gltf`
- `assets/hulk_infinity_hulk/scene.gltf`
- `assets/luna_snow_-_sonic_trailblazer/scene.gltf`
- `assets/wolverine_-_wolverine_-_x-2099_bundle/scene.gltf`

These validation assets are separate from the current OBJ-based CPU runtime scene under `PathTracingRenderer/models/` and `PathTracingRenderer/textures/`. The Vulkan compute preview exposes these glTF scenes through the model selector, while the CPU path still uses the OBJ scene.

## PBR And Vulkan Direction

PBR material import does not require ray tracing. A Vulkan graphics/raster
pipeline can evaluate Cook-Torrance PBR in a fragment shader using direct lights,
shadow maps, probes, lightmaps, or IBL for incoming light. Path tracing is the
separate transport algorithm that traces rays and bounces through the scene.

N-Ray is combining them because the Vulkan target is a compute path tracer: once
the shader finds a surface hit, it still needs physically plausible glTF material
data to decide how light is absorbed, reflected, refracted, emitted, or scattered
into the next ray. The current glTF Vulkan compute preview is now a progressive
path-traced renderer: it flattens glTF geometry into SSBOs, traverses the BVH in
compute, samples base-color/metallic-roughness/normal/emissive/transmission
textures, supports direct sun shadows, denoising guide buffers, and accumulates
multi-bounce diffuse/specular/transmission paths.

Current transparency status: thin glass/transmission is wired and visibly affects
the image. Volume refraction is present in the compute shader with front/back
face IOR handling, Snell refraction, a single active medium, Beer's-law
attenuation, material-panel controls, and a preview fallback that gives
transmissive glTF materials a small scene-scaled thickness when
`KHR_materials_volume` is absent. Volume still needs further visual tuning and
validation before it should be treated as final. The preview also treats imported
triangles as two-sided so thin validation geometry stays visible while the
compute path is being stabilized. See
[`docs/gltf-vulkan-pbr-import.md`](docs/gltf-vulkan-pbr-import.md#pbr-is-the-material-model-not-the-transport-algorithm)
for the detailed comparison.

Future `VK_KHR_ray_tracing_pipeline` work is tracked separately from the current
compute preview. Use the repo-local Vulkan skill and Sascha Willems
`raytracingbasic`, `raytracinggltf`, `raytracingtextures`, and
`raytracingshadows` examples for BLAS/TLAS bring-up, glTF geometry/material
descriptors, any-hit transparency, and two-miss shadow-ray pipelines.

## Build With Premake

`premake5.lua` is the source of truth for project files, include paths, source globs, links, and compiler flags. It generates project files under `build/` so it does not overwrite the hand-written root `Makefile`.

Linux/macOS Makefiles:

```bash
make raylib
premake5 gmake2
make -C build config=release_x64
make -C build config=debug_x64
make -C build config=performance_x64
```

Windows Visual Studio:

```bash
premake5 vs2022
```

Open the generated Visual Studio solution under `build/`, select `x64`, then choose `Debug`, `Release`, or `Performance`. The older root solution may exist, but `premake5.lua` should be treated as the maintained configuration.

## Build Configurations

- `Debug`: symbols enabled, no release optimization.
- `Release`: `NDEBUG`, Premake optimization enabled, OpenMP, AVX2 on supported platforms.
- `Performance`: aggressive CPU path tracer build. Linux/macOS add `-O3 -march=native -ffast-math -flto`; Windows adds `/O2 /GL /fp:fast /arch:AVX2` and `/LTCG`.

`Performance` is intended for throughput testing. `-ffast-math` / `/fp:fast` can slightly change floating-point behavior and noise patterns.

## Runtime Controls

- `W/A/S/D`: move camera.
- `Left Ctrl`: move down.
- `Left Shift`: move up.
- Hold `Right Mouse Button`: rotate camera.
- `Left Mouse Button`: debug ray, model selection, or click-to-focus when Pick DOF is enabled.
- Settings panel: render resolution, bounces, samples, rays per pixel, Russian roulette, worker threads, publish rate, camera, sky/sun, tone mapping, and selected material properties.
- Vulkan material panel: per-material albedo, alpha, metalness, roughness, emission, normal scale, and optics controls for coverage, thin transmission, and volume transmission.
- Stats panel: UI timing, displayed samples, worker samples, rays/sec, samples/sec, publish cost, and render progress.

There are no automated tests yet. Current verification is compile plus manual runtime inspection.

## Current CPU Path Trace Architecture

The UI/main thread owns raylib, input, ImGui frame layout, texture upload, and viewport drawing. Path tracing runs on `AsyncRenderWorker` in a background `std::thread`. Each worker launch snapshots render parameters, camera, screen, HDRI environment view, triangles, compact intersection data, materials, and flat BVH nodes into a `RenderWorkload`.

The worker traces progressively into a float `accumBuffer`. It only publishes composed frames at the first few samples, at the configured `Publish Hz`, or at completion. Display-only changes such as Exposure and Contrast recompose the last published accumulation buffer without restarting the render. Integrand changes such as camera movement, geometry/material edits, bounces, resolution, and rays per pixel invalidate and restart sampling.

## CPU Optimization Highlights

This fork includes several high-level CPU path tracing optimizations:

- **Async worker decoupling**: ImGui stays responsive while rendering continues on a worker thread.
- **Fused sample loop**: ray generation, tracing, and accumulation happen inside one OpenMP pixel loop, avoiding old full-frame ray/state staging passes.
- **Thread control**: worker loops use explicit `num_threads(...)`; by default one hardware thread is reserved for the UI.
- **Dynamic OpenMP scheduling**: `schedule(dynamic, 1024)` balances expensive glass/volume pixels against cheap background pixels.
- **Small deterministic RNG**: hot-path `mt19937`/distribution construction was replaced by a lightweight per pixel/sample/ray generator.
- **Binned SAH BVH build**: BVH construction uses 8-bin surface-area heuristic splitting with a leaf target of 6 triangles.
- **Flat ordered BVH traversal**: flattened nodes store child metadata so traversal can visit the near child first and shrink `closestT` earlier.
- **Compact intersection records**: `TriIntersect` mirrors only the geometry needed for intersection, so traversal avoids streaming the full material-heavy `Tri`.
- **Material indirection**: triangles store `materialIdx`; material data lives once in `data.materials`.
- **Russian roulette**: optional unbiased low-energy path termination reduces average bounce depth.
- **Publication throttling**: the worker does not tone-map and copy a full display frame after every sample unless the viewport needs it.

These optimizations are still CPU-only. Vulkan dependencies are present so the next renderer can be introduced behind explicit render workload/data boundaries instead of coupling GPU work to the ImGui loop.
