# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

N-Ray is a CPU path tracing renderer (educational project). It uses **raylib** for windowing/input/texture display, **Dear ImGui** (via rlImGui) for the UI, **glm** for math, and **OpenMP** for multithreading. Rendering is progressive: samples accumulate over frames until the camera moves.

## Build & Run

Requires a C++20 compiler, `premake5`, OpenMP, and raylib. Builds with AVX2 (`-mavx2`).

```bash
make                  # build Release: vendored raylib -> premake gmake2 -> compile
make CONFIG=debug_x64 # debug build (symbols, no optimization)
make run              # build, then run from PathTracingRenderer/ (assets resolve there)
make raylib           # (re)build vendor/raylib/src/libraylib.a only
make generate         # regenerate build/ project files only
make clean            # rm -rf build bin obj (keeps the raylib lib)
make distclean        # also clean the vendored raylib objects/lib
```

The root `Makefile` is a hand-written wrapper. `premake5.lua` sets `location "build"`, so `premake5 gmake2` generates the workspace makefiles **under `build/`** (not the repo root — don't remove `location` or premake will clobber the wrapper `Makefile`). The compiled binary lands at `bin/<Config>/PathTracingRenderer`. On Windows, open `PathTracingRenderer.sln` in VS2022 (x64). `premake5.lua` is the source of truth for project config (sources, include dirs, per-platform links/flags) — edit it, not the generated files.

**raylib on Linux is built from the vendored source** at `vendor/raylib/` (there is no system raylib and the headers under `PathTracingRenderer/external/raylib/` ship without a compiled library). The wrapper `make` builds `vendor/raylib/src/libraylib.a` via raylib's own Makefile; `premake5.lua` adds `vendor/raylib/src` to `includedirs` (ahead of the bundled headers, so versions match) and `libdirs`. **`vendor/raylib/src/config.h` must have `SUPPORT_FILEFORMAT_HDR 1`** — raylib disables HDR loading by default, and the renderer hard-depends on loading `textures/HDRI.hdr`. If the lib is built without it, `LoadImage` returns an empty image; the renderer now falls back to the procedural sky (`hdriLogic` guards against a null/zero-size image) instead of crashing, but the HDRI won't appear.

**Run from a directory containing `models/` and `textures/`** (use `make run`, or `cd PathTracingRenderer`). Asset paths are relative to the working directory (CWD), not the binary, and these assets live under `PathTracingRenderer/`. The default executable loads `models/scene.obj` and `textures/HDRI.hdr` at startup; a missing OBJ prints "Could not open file" and is skipped rather than aborting (e.g. `models/dragon.obj`, referenced in `main()` but not present). Note `SetTraceLogLevel(LOG_NONE)` suppresses raylib's own warnings, so asset-load failures are otherwise silent.

There are **no automated tests** — verification is manual (run the renderer, move the camera, use the debug ray / stats panel). See README.md for controls (WASD + RMB camera, LMB debug ray / model select / click-DoF).

## Architecture

Everything is driven from `src/Main.cpp`'s `main()` loop. Most subsystems are plain structs instantiated as **globals** there (`params`, `data`, `screen`, `pt`, `myCam`, `ui`, `mRayGen`) plus the two BVH arrays. Per-frame: handle input → update camera → `pt.render(...)` (when `params.render`) → draw 3D rasterized preview or debug rays → ImGui UI.

**Central data flow** revolves around two structs in `include/globalParams.h`:
- `Data` — owns all scene/render buffers: `tris`, `models`, the per-pixel `rays`/`rayStates`, the `frameBuffer` (8-bit RGBA shown on screen) and `accumBuffer` (float HDR accumulation).
- `Params` — all tunable render settings (resolution `res`, `maxBounces`, `maxSamples`, sky/sun, exposure, contrast) and frame flags. **`shouldSample`** gates accumulation: it's set false when the camera moves so the image restarts; `currentSample` counts accumulated samples toward `maxSamples`.

**Geometry & materials (`include/tri.h`, `include/model.h`).** `Tri` is a "fat" struct: each triangle carries its full material (albedo, IOR, roughness, metalness, refraction, absorption, volume, emission, etc.) *and* precomputed geometry (normal, AABB min/max, center). A `PTModel` is a logical object that owns indices into `data.tris` and mirrors the material params; editing a material in the UI calls `PTModel::updateTris()` (`src/model.cpp`) to push values down to its triangles. `ObjImporter` (`include/objImporter.h`) is a constructor-as-loader: instantiating it parses an OBJ, creates one `PTModel`, and appends triangles — material is passed as constructor args, so **scene composition is hardcoded in `main()`** as a series of `ObjImporter{...}` declarations.

**Acceleration (`include/bvh.h`, BVH code in `src/renderer.cpp`).** Two representations:
- `BVH` — recursive build node. Construction is in the constructor: compute AABB, split by the average centroid along the longest axis, partition `data.tris` in place via swaps, recurse. Build nodes accumulate into the global `globalBVH` vector (`createFlatBVH()` in Main.cpp).
- `CompactBVH` — cache-friendly flattened node produced by `PathTracer::flattenBVH()` into `globalCompactBVH`. Traversal (`traverseFlatBVH`) is **stackless**: leaves store `startIndex`/`triCount`; interior nodes store a `missLink` to jump to on AABB miss (`startIndex` and `missLink` share a union).

Both `globalBVH` and `globalCompactBVH` are `extern` globals (declared in `bvh.h`, defined in `Main.cpp`) and referenced directly inside `renderer.cpp` rather than passed everywhere.

**Path tracing core (`src/renderer.cpp`, `include/renderer.h`).** `PathTracer::render` runs `raysPerPixel` passes per frame; each pass:
1. `rayGeneration` — OpenMP `parallel for collapse(2)` over the pixel grid; builds a physically-based camera ray per pixel with sub-pixel jitter (anti-aliasing) and aperture disk sampling (depth of field).
2. `rayLogic` per ray — OpenMP `parallel for`; the bounce loop traverses the BVH, then branches by material: cosine-weighted `diffuseLighting`, GGX microfacet `specularLighting` (Fresnel-Schlick, metalness-aware), `refractionLighting` (Snell + total internal reflection + Beer's-law absorption via `throughput`), and homogeneous-medium volume scattering. Emission adds `throughput * emissionCol`. Rays that escape sample the environment via `hdriLogic` (equirectangular HDRI lookup); `sky()` is an alternative procedural environment (currently commented out at the call site).
3. Accumulate each ray's `col` into `accumBuffer`.

`drawScreen` then divides the accumulator by sample count, applies exposure, an S-curve contrast (`contrastSCurve`), and gamma 2.2, writes the `frameBuffer`, and blits it via a raylib texture.

**Interaction (`include/mouseRay.h`).** `MouseRay::mouseRay` reconstructs a primary ray through the cursor, reused for three features in Main.cpp: debug ray visualization (`traceDebugRay`, draws the bounce path as cylinders), click-to-select model (`selectModel`), and click-to-focus DoF (`setDofDist`).

**Rendering vs. preview.** When `params.render` is false, `main()` draws the scene as a fast flat-shaded rasterized preview with raw `rlgl` triangles instead of path tracing — useful for navigating before committing to a render.

## Conventions & gotchas

- `include/ui.cpp` lives in `include/` (not `src/`) but is compiled — `premake5.lua` globs `include/**.cpp`. Keep that glob in mind when adding files.
- `PI` comes from raylib (`raylib.h`), not a project header.
- Coordinate convention is **Z-up** (world up is `{0,0,1}`).
- New material parameters must be threaded through several places in lockstep: `Tri`, `PTModel` (+ its constructor and `updateTris`), `ObjImporter`'s constructor/argument list, the `ObjImporter{...}` calls in `main()`, and the UI in `ui.cpp`.
