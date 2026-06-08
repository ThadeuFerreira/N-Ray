# Vulkan Denoiser Scaffold

## Current change

This branch introduces denoising as a Vulkan-preview interface and debug scaffold only. It does not implement filtering yet, and it does not alter the unbiased path-tracing accumulation path.

The ownership rule is:

```text
accumulation buffer = truth
denoiser output    = temporary display preview
```

The CPU path tracer, `AsyncRenderWorker`, and CPU accumulation behavior remain unchanged.

## Added interfaces

`PathTracingRenderer/include/vulkan_compute_preview.h` now exposes:

- `VulkanDenoiserMode`
- `VulkanDenoiserDebugView`
- `VulkanDenoiserSettings`
- `VulkanDenoiserStats`

`VulkanPreviewSettings` now carries `denoiser` settings, and `GpuStats` now reports denoiser state for future UI/status panels.

The internal `VulkanDenoiser` scaffold in `PathTracingRenderer/src/vulkan_compute_preview.cpp` currently handles:

- create, destroy, resize, and history reset hooks
- target storage-image format support checks
- denoise-strength fade calculation
- À-trous pass-count selection policy
- per-frame active/skipped decision reporting
- reset-count and target-resource byte accounting

## Debug logs

Logs use the `[VulkanDenoiser]` prefix.

Creation and resize logs include:

- extent
- target resource byte estimate
- required storage-image format support
- scaffold status

Per-frame skip logs include:

- skip reason
- mode
- debug view
- sample count
- denoise strength
- planned pass count
- firefly-clamp setting

Current expected skip reasons include:

- `mode off`
- `non-model shader`
- `first-hit feature resources pending`
- `unsupported storage-image format`
- `sample count past denoise fade-out`
- `denoise passes not implemented yet`

Verbose per-frame logging can be enabled with `VulkanDenoiserSettings::verboseLogging` or the `NRAY_VULKAN_DENOISER_LOG` environment variable.

## Intentional non-goals

- No denoised pixels are written yet.
- No denoiser image resources are allocated yet.
- No first-hit feature buffers are emitted yet.
- No tone-map source switch is active yet.
- No denoised image is fed back into accumulation.
- No temporal reprojection or SVGF history is implemented.

## Next phase

The next phase should make the display-side postprocess real while preserving the current accumulation contract.

Implement in this order:

1. Add GPU-side output resources for resolved HDR and first-hit features.
2. Extend the glTF model-preview compute shader to write first-hit normal, albedo, roughness, depth, material id, and primitive or instance id.
3. Resolve the current accumulation average into an HDR buffer before tone mapping.
4. Add a denoiser prepare pass that reads resolved HDR and feature buffers and writes ping.
5. Add one 5x5 cross-bilateral or first À-trous pass from ping to pong.
6. Composite raw HDR and denoised HDR using the sample-count fade policy.
7. Add debug-view display modes for raw, normal, albedo, depth, material id, and denoised output.

Only after the spatial filter is stable should temporal history or SVGF-lite state be added.
