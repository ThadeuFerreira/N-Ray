# Path Trace Render Performance Investigation

## Summary

The recent render-worker split fixed the biggest responsiveness problem: Dear ImGui no longer has to wait for path tracing work to finish before it can process input and redraw. The remaining performance problem is render throughput. The current path tracer still spends a large amount of time in CPU sampling, BVH traversal, memory movement, random-number generation, and full-frame publication.

This report is based on static inspection of the current renderer and worker code. It does not include runtime profiling numbers yet. The next step should be to establish a repeatable benchmark scene and measure each recommendation before and after implementation.

## Vulkan Preview Lighting Regression Note

This file originally focused on the CPU path tracer, but the current primary
performance surface is the Vulkan glTF compute preview. The June 2026
environment-lighting pass introduced a concrete regression that is worth keeping
as a future checklist item: disabled analytic lights must not trace shadow rays.

The slow path was caused by direct sun shadow visibility being evaluated even
when the sun was disabled. In a ray-traced shadow mode this made a "sun off"
configuration still launch finite BVH visibility rays, which looked like a
general sample-time regression. The fix is to short-circuit direct sun lighting
before any visibility work when the sun toggle or sun intensity is off.

The 3-point key/fill/rim lights now default to unshadowed direct lighting for
performance. Finite-distance BVH shadow rays for those lights are still useful
for diagnosis and quality comparisons, but they are an explicit debug option
(`Point Light Shadows` in `Scene -> Vulkan -> Lighting Debug`, or
`--point-light-shadows` in `NrayRenderDocHeadless`). Keep that default unless a
future feature specifically budgets for the extra visibility rays.

Use the headless tool for quick A/B checks before changing lighting code:

```bash
cd PathTracingRenderer
../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --width 128 --height 128 --samples 1 --max-bounces 3 \
  --shadow ray-traced --lighting-log --json-out /tmp/nray_light_base.json

../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --width 128 --height 128 --samples 1 --max-bounces 3 \
  --shadow ray-traced --point-light-shadows --lighting-log \
  --json-out /tmp/nray_light_point_shadows.json
```

Compare `gpuDispatchMs` and the JSON `lighting` object. The first run should
log `pointShadows=0`; the second should log `pointShadows=1` and is expected to
be slower. If the baseline looks as slow as the point-shadow run, check
`directSunLighting`, `directPointLighting`, and the `GpuSettings` feature flags
before assuming the denoiser or HDRI sampler is at fault.

## Current Render Flow

The main loop calls the path trace update from `PathTracingRenderer/src/app_loop.cpp`. When sampling is enabled, `AsyncRenderWorker` snapshots the current params, camera, screen, HDRI handle, and triangle list, then renders on a background thread.

Inside `AsyncRenderWorker::run()` the worker allocates full-resolution arrays for rays, ray states, the accumulation buffer, and the display frame buffer. For each completed sample it currently performs this sequence:

1. Generate one ray and one ray state for every pixel with `PathTracer::rayGeneration()`.
2. Trace every generated ray with `PathTracer::rayLogic()`.
3. Add every ray result into `accumBuffer`.
4. Tone-map the entire `accumBuffer` into `frameBuffer`.
5. Copy `frameBuffer` into the worker handoff buffer.
6. The main thread copies the handoff buffer again and uploads it with `UpdateTexture()`.

The stats panel reads atomics from the worker once per UI frame, which is the right direction. The current stats path is not the main performance risk because it avoids per-ray instrumentation.

## Main Bottlenecks

### Full-Frame Passes and Memory Traffic

The hottest worker loop is split into separate full-frame passes: ray generation, ray tracing, accumulation, composition, and publication. This creates unnecessary memory traffic because `rays` and `rayStates` are written, read, then mostly discarded for every rays-per-pixel iteration.

The highest-impact change is to fuse ray generation, tracing, and accumulation into one parallel pixel loop. Each worker iteration should create local `PathRay` and `PathRayState` values, trace them, then add directly into `accumBuffer[pixelIndex]`. That removes most writes to `Data::rays` and `Data::rayStates`, improves cache locality, and reduces OpenMP region setup cost.

Recommended shape:

```cpp
#pragma omp parallel for schedule(static)
for (int i = 0; i < pixelCount; ++i) {
    PathRay ray;
    PathRayState state;
    generatePixelRay(i, ray, state, camera, screen, params, rng);
    traceRay(ray, state, tris, params, hdri, rng);
    accumBuffer[i] += state.col;
}
```

Keep the debug-ray path separate. It only traces one mouse ray and does not need to share the same optimized hot loop.

### RNG Overhead

The renderer uses `std::mt19937` in multiple hot functions and constructs `std::uniform_real_distribution<float>` inside per-pixel or per-bounce code. `mt19937` has a large state and is slower than necessary for path tracing samples.

Replace the hot-path RNG with a small generator such as PCG32, xoshiro, or splitmix-derived state. Seed it from stable inputs such as pixel index, sample index, ray index, and thread id. Provide a simple inline `randomFloat01()` function instead of constructing distribution objects in the inner loop.

This will change the noise pattern, so validation should compare convergence and visual stability rather than exact images.

### Frame Composition and Publication Cost

The worker tone-maps and publishes a full frame after every completed sample. At higher resolutions this can become a visible bandwidth cost, especially because the current handoff copies the display buffer at least twice before texture upload.

Throttle publication independently from sampling. The worker can keep tracing continuously but only compose and publish:

- the first few samples, so the viewport responds immediately;
- then at a capped display rate such as 30 or 60 Hz;
- and always once at completion.

The handoff should also move toward double or triple buffering. Instead of copying `pendingFrame = workerData.frameBuffer` and then copying again on consume, use fixed buffers and swap ownership under a short mutex. The main thread should only upload frames that will actually be displayed.

### OpenMP Can Still Starve the UI

The path tracer runs on a background `std::thread`, but each worker phase uses OpenMP. By default OpenMP may consume all cores, leaving the main thread responsive in structure but starved by CPU scheduling.

Reserve at least one hardware thread for the UI shell. Make the worker thread count configurable and default it to `max(1, hardware_concurrency - 1)`. Use `schedule(static)` for image-sized loops unless profiling shows work imbalance from bounce variance is large enough to justify dynamic scheduling.

Measure both render throughput and UI frame time. The best setting may not be the highest raw rays-per-second value if it causes editor input to stutter.

### BVH Quality and Traversal

The current BVH build uses an average centroid split on the longest axis and stops splitting below three triangles. This is simple, but it can produce poor partitions and very small leaves. Small leaves reduce triangle tests per leaf, but they can increase node traversal overhead.

Investigate a binned SAH builder with a tunable leaf size, likely in the 4 to 8 triangle range. The optimal value should be measured per scene. Also consider storing enough information during AABB tests to visit the nearer child first, allowing closer hits to reduce later traversal work.

`CompactBVH` should be evaluated for cache layout. Alignment, node size, and whether min/max bounds are stored as AoS or SoA can matter because every ray touches many BVH nodes.

### Triangle and Material Layout

`Tri` stores intersection geometry, normals, bounds, material colors, PBR parameters, volume fields, ids, and flags together. Traversal needs only a small subset of that data for most tests, but the current layout pulls a large structure through cache.

Split geometry from material data. A compact geometry triangle should hold positions or precomputed edges, normals needed for intersection, bounds, and a material id. Material properties should live in a separate array indexed after a hit is found. This reduces memory bandwidth in traversal and also avoids duplicating material data across every triangle belonging to the same model.

This is a larger refactor because OBJ import, material editing, selection, and PBR model data all currently interact with triangle storage.

### Duplicate Hit Math

`PathTracer::InterpolateNormal()` recomputes barycentric coordinates from the hit position. The triangle intersection path already computes data related to the hit and could return barycentric coordinates with the closest hit.

Store `hitU` and `hitV` in `PathRayState`, or return them from the intersection routine when a closer triangle is accepted. Normal interpolation can then reuse those values directly. This removes several dot products from every shaded hit.

Also review `RayIntersectsTriangle()` against `Tri::eA` and `Tri::eB`. The triangle already precomputes edges, so the intersection routine should not recompute `b - a` and `c - a` if the stored orientation matches the algorithm.

### HDRI and Sky Lookup

`hdriLogic()` performs duplicate clamping of the HDRI coordinates. That is minor compared with traversal, but it is in a hot path for rays that miss the scene. Remove the duplicate clamps and cache simple HDRI metadata such as width, height, and float data pointer in the render snapshot.

Longer term, environment importance sampling will likely improve convergence more than it improves raw rays per second. It should be treated as a quality-per-sample optimization.

## Recommended Implementation Phases

### Phase 1: Low-Risk Measurement and Scheduling

Start by making measurement reliable. Add a benchmark mode or repeatable scene settings that report samples/sec, MRays/sec, render time, publish time, and UI frame time. Keep timing outside the inner ray loop.

Then limit OpenMP worker threads so the UI keeps a reserved core. Remove duplicate HDRI clamps and add publication throttling so display updates are not forced every sample.

Expected outcome: better UI stability under load and clearer numbers for deeper optimization.

### Phase 2: Hot Loop Restructure

Fuse ray generation, ray tracing, and accumulation into one parallel loop. Replace hot-path `std::mt19937` usage with a lightweight RNG passed through generation and bounce code. Keep the current debug ray workflow separate so editor diagnostics stay simple.

Expected outcome: lower memory bandwidth, fewer full-buffer writes, fewer OpenMP phase transitions, and improved rays per second.

### Phase 3: Data Layout and BVH

Split triangle geometry from material data, tune BVH leaf size, and evaluate a binned SAH builder. Consider aligned node storage and traversal ordering based on ray distance to child bounds.

Expected outcome: better scaling on triangle-heavy scenes and fewer cache misses during traversal.

### Phase 4: Quality Per Sample

After raw throughput improves, focus on convergence. Candidates include environment importance sampling, emissive-triangle direct light sampling, Russian roulette termination, and material-specific sampling improvements.

Expected outcome: fewer samples required for the same perceived quality.

## Profiling Plan

Use release builds for performance work:

```bash
make CONFIG=release_x64
```

Measure with a fixed scene, fixed camera, fixed resolution, fixed max bounces, fixed rays per pixel, and fixed sample count. Suggested starting points:

- 512x512, 1 ray per pixel, 5 bounces, 100 samples.
- 1024x1024, 1 ray per pixel, 5 bounces, 50 samples.
- One simple scene and one triangle-heavy scene.

Record:

- total rays traced;
- render elapsed seconds;
- MRays/sec;
- samples/sec;
- average sample milliseconds;
- frame publish count and publish milliseconds;
- UI average frame milliseconds while rendering.

Linux profiling commands to consider:

```bash
perf stat ./bin/Release/PathTracingRenderer
perf record ./bin/Release/PathTracingRenderer
perf report
```

For deeper investigation, add scoped timers around worker phases only: generation/trace/accumulate, compose, publish, and texture upload. Avoid timers inside `rayLogic()` or intersection loops unless they are behind a compile-time profiling flag.

## Success Criteria

A performance change should be considered successful only when it improves measured render throughput without regressing UI responsiveness or image correctness. Minimum acceptance checks:

- MRays/sec improves on the benchmark scene.
- UI average frame time remains stable while rendering.
- Render completion still reaches `maxSamples`.
- Stats panel values remain tied to worker progress, not UI FPS.
- Debug ray tracing and selection still work.
- Visual output remains plausible after RNG or sampling changes.

## Risks

RNG replacement can alter noise distribution and make exact image comparisons unreliable. BVH and triangle-layout changes can affect selection, debug rays, material editing, and imported model behavior. OpenMP thread limits can lower peak throughput on machines where the UI is not competing heavily, so the thread count should be configurable.

The safest path is to measure first, then land changes in small phases with benchmark numbers attached to each change.
