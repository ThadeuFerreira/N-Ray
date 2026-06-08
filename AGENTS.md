# AGENTS.md

This file provides guidance to coding agents working in this repository. It mirrors `CLAUDE.md` — keep the two in sync when either changes.

## Overview

N-Ray's current primary engineering direction is Vulkan GPU rendering and optimization.
The long-term target is a full Vulkan compute/HW-accelerated path-tracing path, with
the existing CPU path tracer retained as a known-good reference/fallback. The renderer
uses **raylib** for windowing/input/texture display, **Dear ImGui** (via rlImGui) for
the UI, **glm** for math, and **OpenMP** for CPU multithreading. Rendering is
progressive and runs on a **background worker thread**: samples accumulate until the
camera moves or a render setting changes.

## Scope policy

- Vulkan-first development is the default for all optimization and new feature work.
- Do **not** modify `PathTracer`/CPU traversal/shading internals, `AsyncRenderWorker`,
  or other CPU path-tracer hot paths unless the user explicitly asks for CPU path-tracer
  work.
- If a task can be done entirely in the Vulkan stack (preview, denoising,
  glTF upload/shading, synchronization, descriptor layout, GPU debug/profiling), keep CPU
  code unchanged.

## Skills

Project skills live in `.claude/skills/` as one folder per skill, each with a `SKILL.md` using the same frontmatter/body model as Codex skills. Treat this as the single repo-local skill source for Claude Code and other agents, and keep these links in sync between `CLAUDE.md` and `AGENTS.md`.

- `.claude/skills/nray-vulkan-tutorials/SKILL.md` - use for Vulkan issues, compute shader experiments, Tutorial28/VulkanCore references, vendored glTF import reference examples (`tutorials/saschawillems/gltf/`), upstream glTF skinning guidance, descriptor/synchronization/debugging work, Vulkan PBR pipelines (material push constants, IBL pre-computation via BRDF LUT/irradiance cube/prefiltered cube, textured PBR with tangent vertex attributes), hardware ray tracing (VK_KHR_ray_tracing_pipeline, BLAS/TLAS build, SBT layout, raygen/miss/closesthit/anyhit/intersection/callable shader groups, frame accumulation, glTF ray tracing with descriptor indexing, recursive secondary rays for shadows and reflections — multiple miss shaders, ray payloads, iterate-in-raygen bounce loops), and porting the CPU path tracer toward Vulkan compute or HW ray tracing.
- `.claude/skills/cpp-smart-pointers/SKILL.md` - use for C++ ownership/lifetime changes, asset/resource registry design, buffer/texture/mesh lifetime reviews, or audits for hidden allocations and smart-pointer traffic in hot render paths.
- `.claude/skills/task-completion-gates/SKILL.md` - use for mandatory completion checks: never finish work until relevant build/link targets succeed.

When a skill applies, read its `SKILL.md` before designing or changing code.

## Build & Run

Requires a C++20 compiler, `premake5`, OpenMP, and raylib. Builds with AVX2 (`-mavx2`).

```bash
make                   # build Release: vendored raylib -> premake gmake2 -> compile
make CONFIG=debug_x64  # debug build (symbols, no optimization)
make build-performance # Performance config: -O3 -march=native -ffast-math -flto
make run               # build Release, then run from PathTracingRenderer/ (assets resolve there)
make run-performance   # build + run the Performance binary
make raylib            # (re)build vendor/raylib/src/libraylib.a only
make generate          # regenerate build/ project files only
make clean             # rm -rf build bin obj (keeps the raylib lib)
make distclean         # also clean the vendored raylib objects/lib
```

### Completion gate

- Before ending a task, run the relevant build target(s) for the modified code path and ensure they complete without compile or link errors.
- If any build/link failure appears, fix the error(s) and rerun until the command is green.

`premake5.lua` defines three configurations: **Debug**, **Release**, **Performance**. The root `Makefile` is a hand-written wrapper. `premake5.lua` sets `location "build"`, so `premake5 gmake2` generates the workspace makefiles **under `build/`** (not the repo root — don't remove `location` or premake will clobber the wrapper `Makefile`). The compiled binary lands at `bin/<Config>/PathTracingRenderer`. On Windows, open `PathTracingRenderer.sln` in VS2022 (x64). `premake5.lua` is the source of truth for project config (sources, include dirs, per-platform links/flags) — edit it, not the generated files.

**raylib on Linux is built from the vendored source** at `vendor/raylib/` (there is no system raylib and the headers under `PathTracingRenderer/external/raylib/` ship without a compiled library). The wrapper `make` builds `vendor/raylib/src/libraylib.a` via raylib's own Makefile; `premake5.lua` adds `vendor/raylib/src` to `includedirs` (ahead of the bundled headers, so versions match) and `libdirs`. **`vendor/raylib/src/config.h` must have `SUPPORT_FILEFORMAT_HDR 1`** — raylib disables HDR loading by default, and the renderer wants to load `textures/HDRI.hdr`. If the lib is built without it, `LoadImage` returns an empty image; the renderer falls back to the procedural sky (`hdriLogic`/`makeRenderEnvironment` guard against an invalid image) instead of crashing, but the HDRI won't appear.

**Run from a directory containing `models/` and `textures/`** (use `make run`, or `cd PathTracingRenderer`). Asset paths are relative to the working directory (CWD), not the binary, and these assets live under `PathTracingRenderer/`. The scene is composed in `loadSceneLayer()` as a series of `ObjImporter{...}` declarations; a missing OBJ prints "Could not open file" and is skipped rather than aborting (e.g. `models/dragon.obj`, referenced but not present). Note `SetTraceLogLevel(LOG_NONE)` suppresses raylib's own warnings, so asset-load failures are otherwise silent.

**Top-level `assets/` is the validation corpus for glTF/Vulkan importer work.** Use local `assets/*/scene.gltf` or `.glb` bundles as the first source of validation models before downloading external samples. Keep each bundle's scene file, binary buffers, textures, and license/source files co-located. These validation assets are separate from the current `PathTracingRenderer/models/` OBJ runtime scene and should be used when checking future glTF parsing, PBR material conversion, texture color-space handling, and Vulkan upload paths.

**Vulkan compute preview model list lives in `PathTracingRenderer/project_settings.json`, not in code.** The "Vulkan Mode" model dropdown is populated from that manifest (`{"models":[{"name","folder"}, …]}`). There are **no hardcoded models** — `vulkan_compute_preview.cpp` reads the manifest once via `activeModelEntries()`/`loadModelEntriesFromSettings()` (folders resolve relative to CWD and its parent, so repo-root `assets/<dir>` entries work when run from `PathTracingRenderer/`). The "Import Model Folder" / Browse / Paste UI calls `VulkanComputePreview::importModelFromFolder`, which finds the folder's `scene.gltf`/`.glb`, appends a `ModelEntry`, and **persists the updated list back to `project_settings.json`** (rewriting folders to canonical paths) so imports survive across runs. Missing folders are logged and skipped, not fatal. No model is loaded into GPU buffers at startup (`selectedModel` starts at `-1`): the preview always comes up for the shader-only modes, and selecting/importing a model lazily uploads its buffers and switches to the model-preview shader. Touching the model list from more than the main thread would need synchronization — today it is main-thread only.

**Vulkan glTF transparency/refraction status.** The current compute preview decouples alpha coverage from physical transmission: `alphaMode` controls coverage/cutout behavior, while `KHR_materials_transmission` and `KHR_materials_ior` drive the transmission/refraction path. Thin glass/transmission is working and visibly affects the output image. Volume refraction is wired through material upload and `vulkan_gltf_flat.comp` (front/back-face IOR, Snell refraction, single-medium tracking, Beer's-law attenuation, and material-panel controls), and the preview assigns a small scene-scaled thickness to transmissive glTF materials when `KHR_materials_volume` is absent so existing car glass can exercise that path. Volume still needs more visual tuning and validation; document it as in progress, not finished. Ray-traced shadows attenuate through transmissive materials; shadow-map mode remains approximate for transparent/volumetric casters.

There are **no automated tests** — verification is manual (run the renderer, move the camera, use the debug ray / stats panel). For a quick headless throughput check, `src/renderer.cpp` can be compiled against a tiny standalone harness with no window (leave `RenderEnvironment` invalid to use the procedural sky). See README.md for controls (WASD + RMB camera, LMB debug ray / model select / click-DoF).

## Architecture

`src/Main.cpp` is a thin entry point: `configureApplication()` → `startupWindow()` → `loadSceneLayer()` → `initializeRenderLayer()` → `startupRuntimeLayer()` → `runMainLoop()` → shutdown. The code is split into small translation units rather than one monolith:

- **`src/app_state.cpp`** — defines the globals (`params`, `data`, `screen`, `pt`, `myCam`, `ui`, `mRayGen`, `cam3D`, plus `globalBVH`/`globalCompactBVH`), declared `extern` in `include/app.h`. Also window lifecycle and the render-texture / `RenderEnvironment` helpers.
- **`src/startup_layers.cpp`** — `loadSceneLayer` (hardcoded `ObjImporter{...}` scene), `initializeRenderLayer` (BVH build via `createFlatBVH` + `flattenBVH`, per-triangle index assignment, `triIsect` mirror build, camera init), `startupRuntimeLayer` (HDRI load, render texture, ImGui).
- **`src/app_loop.cpp`** — `runMainLoop` and its per-frame helpers (UI hover state, render-target resize, sampling gate, viewport actions, driving the async worker, drawing).
- **`src/render_worker.cpp` / `include/render_worker.h`** — the async render worker (below).
- **`src/interaction.cpp`** — mouse-ray features. **`src/viewport_preview.cpp`** — raster preview.
- UI is split across **`include/ui*.cpp`** (compiled via the `include/**.cpp` glob).

**Central data flow** revolves around two structs in `include/globalParams.h`:
- `Data` — `tris` (the fat `Tri`), `triIsect` (compact intersection mirror), `materials` (`PBRMaterial`), `models` (`PTModel`), the `frameBuffer` (8-bit RGBA shown on screen) and `accumBuffer` (float HDR accumulation).
- `Params` — all tunable render settings (`res`, `maxBounces`, `maxSamples`, `raysPerPixel`, sky/sun, exposure, contrast, `russianRoulette`/`rrMinBounces`, `renderWorkerThreads`, `renderPublishHz`) and the frame-gate flags. **`shouldSample`** gates accumulation; **`renderInvalidated`** forces a worker restart (camera/scene/setting change); **`displayInvalidated`** recomposes the existing accumulation buffer without re-tracing (exposure/contrast tweaks). `currentSample` counts accumulated samples toward `maxSamples`. **`maxSamples` is clamped to `[kMinSamples, kMaxSamples]` (= `[1, 1000]`, default `10`)** — these `inline constexpr` constants in `globalParams.h` are the single source of truth shared by the UI slider (`ui_render_settings.cpp`), the Vulkan-preview driver (`makeVulkanPreviewSettings`), and the per-frame clamp in `updatePathTraceRender`; the GPU dispatch only guards positivity. Don’t re-introduce literal min/max values — change `kMinSamples`/`kMaxSamples` if needed.

**Geometry & materials (`include/tri.h`, `include/pbr_model.h`).** Materials are **not** baked into triangles anymore: `PBRMaterial` (albedo, IOR, roughness, metalness, refraction, absorption, volume, emission, etc.) lives in `data.materials`, and each `Tri` stores a `materialIdx` (plus `modelIdx`). `Tri` is still "fat" — it precomputes geometry (verts, vertex normals, edges `eA`/`eB`, face normal, AABB min/max, center). `TriIntersect` is a compact 44-byte parallel record (`a, eA, eB, idx, doubleSided`) that BVH traversal reads **instead of** the ~160-byte `Tri`; the fat `Tri` is only touched on the final hit (normals) and during shading (material lookup). `PTModel` owns triangle indices and a `materialIdx`; the UI edits `data.materials[...]` and calls `PTModel::updateTris` (`src/pbr_model.cpp`), which pushes `doubleSided` down to its triangles. `ObjImporter` (`include/objImporter.h`) is a constructor-as-loader: instantiating it appends one `PBRMaterial`, one `PTModel`, and the parsed triangles — so **scene composition is hardcoded in `loadSceneLayer`**.

**Acceleration (`include/bvh.h`, code in `src/renderer.cpp`).** Two representations:
- `BVH` — recursive build node. The constructor computes the AABB and splits via a **binned SAH** (`BVH_SAH_BINS`, 8) — for each axis it bins centroids, sweeps to find the lowest surface-area×count split, and partitions `data.tris` in place by swaps; it falls back to a median (`nth_element`) split along the longest extent when SAH finds no useful plane. Leaves hold up to `BVH_LEAF_TRIANGLE_COUNT` (6) triangles. The chosen `splitAxis` is recorded for ordered traversal. Build nodes accumulate into `globalBVH`.
- `CompactBVH` — flattened node (PBRT layout) produced by `PathTracer::flattenBVH` into `globalCompactBVH`: the **first child sits immediately after the parent**, so only `secondChild` is linked; `axis` is stored. Leaves store `startIndex`/`triCount` (`startIndex` shares a `union` with `secondChild`).
- `traverseFlatBVH` is **ordered + stack-based**: a small explicit stack, visiting the near child first (chosen by `ray.invDir` sign vs. `node.axis`) so `closestT` shrinks quickly and far sub-trees fail the box test. `rayAABB` is a **branchless slab test using the precomputed `ray.invDir`** (no per-node divides).

Both `globalBVH` and `globalCompactBVH` are `extern` globals (declared in `bvh.h`, defined in `app_state.cpp`).

**Async rendering (`src/render_worker.cpp`).** Path tracing runs on a background `std::thread` (`AsyncRenderWorker`), not in the main loop. `start()` snapshots params/camera/screen/environment/scene/BVH into a `RenderWorkload` (copies `tris`, `triIsect`, `materials`, `flatBVH`) and launches `run()`. `run()` loops samples until `maxSamples` or cancel; each sample is an OpenMP `parallel for schedule(dynamic, 1024)` over pixels — per pixel: `makeRenderRng` (seeded deterministically by pixel/sample/ray), `generatePixelRay`, `rayLogic`, accumulate into `accumBuffer`. Finished frames are composed (`composeRenderFrame`: divide by sample count, exposure, S-curve contrast, gamma 2.2) and published under a mutex via a double-buffered `swap`; the main thread polls `consumeFrame` and `UpdateTexture`. Stats (rays/sec, ms/sample, published frames) are atomics read each frame. `renderInvalidated` cancels and restarts the worker; `displayInvalidated` recomposes the existing accum buffer without re-tracing.

**Path tracing core (`src/renderer.cpp`, `include/renderer.h`).** `rayLogic` runs the bounce loop per ray: traverse the BVH, then branch by material — cosine-weighted `diffuseLighting`, GGX microfacet `specularLighting` (Fresnel-Schlick, metalness-aware), `refractionLighting` (Snell + total internal reflection + Beer's-law absorption via `throughput`), and homogeneous-medium volume scattering. Emission adds `throughput * emissionCol`. After `rrMinBounces`, **Russian roulette** probabilistically terminates low-energy paths and reweights the survivors by `1/p` (unbiased — converges to the same image; toggle via `params.russianRoulette`). Rays that escape sample the environment via `hdriLogic` (equirectangular HDRI lookup; falls back to procedural `sky()` when the HDRI is missing). `rayLogic` takes an optional `std::vector<DebugRay>*` — non-null **only** for the debug-ray tool, so the render path allocates nothing per ray.

**Interaction (`include/mouseRay.h`, `src/interaction.cpp`).** `MouseRay::mouseRay` reconstructs a primary ray through the cursor, reused for three features: debug ray visualization (`traceDebugRay`, draws the bounce path as cylinders), click-to-select model (`selectModel`), and click-to-focus DoF (`setDofDist`).

**Rendering vs. preview.** When `params.render` is false, `drawRasterPreview` (`viewport_preview.cpp`) draws the scene as a fast flat-shaded rasterized preview with raw `rlgl` triangles instead of path tracing — useful for navigating before committing to a render.

## Performance (CPU path)

The CPU renderer has been optimized as the **final CPU pass** before the planned Vulkan port — measured at ~2.8–3.2× over the previous version (and bit-identical output for the non-RR optimizations). Key hot-path choices, all in `renderer.cpp` / `bvh.h`:

- **Branchless `rayAABB`** using the precomputed `ray.invDir` — no per-node divisions. Keep `ray.invDir` updated after every direction change (`generatePixelRay`, end of the bounce loop, volume scatter).
- **Compact `TriIntersect` mirror** so traversal streams 44 B/triangle instead of the ~160 B fat `Tri`.
- **Ordered (near-child-first) stack traversal** over the SAH-built tree.
- **OpenMP `schedule(dynamic, 1024)`** to balance wildly uneven per-pixel cost (glass/volume vs. background).
- **Russian roulette** (unbiased) to cut average path length.

When extending the hot path, keep `TriIntersect` small and avoid per-ray heap allocation.

## Conventions & gotchas

- `include/*.cpp` files (UI + widgets) **are compiled** — `premake5.lua` globs `include/**.cpp`. Keep that in mind when adding files.
- Globals are defined once in `src/app_state.cpp` and declared `extern` in `include/app.h`.
- `PI` comes from raylib (`raylib.h`), not a project header.
- Coordinate convention is **Z-up** (world up is `{0,0,1}`).
- `data.triIsect` mirrors `data.tris` geometry and is built once in `initializeRenderLayer` after triangle indices are assigned. It stays valid because geometry and `doubleSided` never change at runtime. **If you ever mutate triangle positions or `doubleSided` at runtime, rebuild `triIsect` (and the BVH).**
- A new material parameter must be threaded through several places in lockstep: `PBRMaterial`, `ObjImporter`'s constructor/argument list and the `ObjImporter{...}` calls in `loadSceneLayer`, `PTModel::updateTris` (if it affects triangles), and the UI (`ui_scene_settings.cpp` + `UI::SelectedMaterialState`).
- The render path runs on a worker thread; per-pixel RNG is seeded deterministically (`makeRenderRng`), so frames are reproducible regardless of thread scheduling.
