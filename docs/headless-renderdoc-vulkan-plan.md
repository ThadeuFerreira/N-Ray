# Headless RenderDoc Vulkan Agent Workflow

This plan defines the first headless RenderDoc capability for N-Ray agents. It
targets the current Vulkan compute preview, not the CPU path tracer and not a
new graphics renderer. The goal is a repeatable way for agents to capture and
summarize Vulkan compute state without driving the raylib/ImGui window by hand.

## Goals

- Produce a deterministic one-frame RenderDoc capture of the Vulkan compute
  preview from a console command.
- Drive `VulkanComputePreview` directly so the tool avoids raylib, rlImGui, and
  the normal OpenGL presentation window.
- Preserve the current compute/BVH architecture and descriptor contract.
- Emit machine-readable run metadata so agents can attach captures, reports,
  selected models, shader modes, and GPU stats to future debugging notes.
- Provide a Python replay/report script for existing `.rdc` files when the
  RenderDoc Python module is available.

## Non-Goals

- Do not add a visual correctness oracle. RenderDoc proves resource state and
  API flow, not final artistic correctness.
- Do not automate shader source stepping in v1. Source stepping still uses the
  shader-debug build from `docs/renderdoc-vulkan-debugging.md`.
- Do not introspect `VK_KHR_ray_tracing_pipeline` work. RenderDoc can replay
  later resources, but raygen/miss/hit bindings and traversal remain opaque.
- Do not edit `PathTracer`, `AsyncRenderWorker`, CPU accumulation, or other CPU
  hot paths for this workflow.
- Do not persist ad-hoc `--model-folder` captures into
  `PathTracingRenderer/project_settings.json` unless a user explicitly asks for
  that import behavior.

## Target Architecture

Add a separate console target, tentatively named `NrayRenderDocHeadless`, rather
than branching the normal `PathTracingRenderer` startup. The target should:

- Compile only the Vulkan preview, glTF import, BVH flattening support, shader
  SPIR-V headers, and small shared data types needed by `VulkanComputePreview`.
- Instantiate `VulkanComputePreview` directly with a fixed render size.
- Select a shader and, for model preview, select either an existing manifest
  model by index or a transient model folder.
- Compute a deterministic camera from the loaded model bounds, matching the
  existing `frameVulkanPreviewModel` intent without depending on UI state.
- Fill `VulkanPreviewSettings` from CLI arguments.
- Arm RenderDoc capture through the existing in-application API hook.
- Run one or more fenced `render()` calls and exit without entering a window
  loop.

The normal app may keep its menu item and `NRAY_RENDERDOC_CAPTURE=1` path. The
headless tool should reuse the same RenderDoc app API controller so manual and
agent captures exercise the same capture mechanism.

## CLI Contract

The v1 command should support these options:

```text
NrayRenderDocHeadless
  --width <pixels>              default 512
  --height <pixels>             default 512
  --shader-index <index>        default glTF model preview when a model is selected
  --model-index <index>         select from PathTracingRenderer/project_settings.json
  --model-folder <path>         transient glTF/GLB folder, not persisted
  --samples <count>             default 1, clamped by kMinSamples/kMaxSamples
  --max-bounces <count>         default 5
  --shadow <none|ray-traced|shadow-map>
  --denoiser <off|spatial-atrous>
  --debug-view <final|raw|denoised|normal|albedo|depth|material-id|instance-id>
  --capture                     arm RenderDoc for the next Vulkan dispatch
  --capture-template <path>     RenderDoc capture path template
  --json-out <path>             write run metadata
  --ppm-out <path>              optional debug image dump from the readback buffer
```

Defaults should favor a small, fast capture:

- `--width 512 --height 512`
- `--samples 1`
- `--shadow ray-traced`
- `--denoiser off`
- first local manifest model if `--model-index` and `--model-folder` are both
  omitted, as long as the model exists and loads

## Capture Flow

Use `renderdoccmd capture` as the outer launcher, with a timeout so automated
agents do not hang when RenderDoc or the Vulkan layer misbehaves:

```bash
mkdir -p build/renderdoc
timeout 45 renderdoccmd capture \
  --wait-for-exit \
  --working-dir /home/thadeu/projects/N-Ray/PathTracingRenderer \
  --capture-file /home/thadeu/projects/N-Ray/build/renderdoc/nray_headless \
  /home/thadeu/projects/N-Ray/bin/Release/NrayRenderDocHeadless \
    --model-index 0 \
    --samples 1 \
    --capture \
    --json-out /home/thadeu/projects/N-Ray/build/renderdoc/run.json
```

The headless binary should also set the RenderDoc capture file template through
the in-app API when `--capture-template` is provided. After capture, query
`GetNumCaptures()` and `GetCapture()` to report the capture path. If the API does
not expose a path, the wrapper can fall back to finding the newest `.rdc` under
`build/renderdoc/`.

Use `timeout` in documented commands because `renderdoccmd capture` can wait on
process or layer state even when the target exits quickly. A successful headless
target must always exit on its own after the requested dispatches.

## Replay And Report Flow

Add a script, tentatively `tools/renderdoc_capture_report.py`, for `.rdc`
inspection. It should:

- Import `renderdoc` only at runtime and print a setup error if the module is not
  available.
- Call `rd.InitialiseReplay(rd.GlobalEnvironment(), [])`.
- Open the capture with `rd.OpenCaptureFile()` and `OpenFile()`.
- Check `LocalReplaySupport()`.
- Open the capture with `OpenCapture(rd.ReplayOptions(), None)`.
- Walk root actions recursively and summarize compute dispatches, labels,
  event ids, draw/dispatch dimensions when available, and pipeline state.
- For selected events, call `GetDescriptorAccess()` and use descriptor queries
  to report bound storage buffers, storage images, sampled images, and samplers.
- Emit JSON, and optionally a concise text table, without requiring qrenderdoc.

Example:

```bash
python3 tools/renderdoc_capture_report.py \
  build/renderdoc/nray_headless_frame1.rdc \
  --json-out build/renderdoc/report.json
```

If `import renderdoc` fails, the script should exit non-zero with a clear message
that the RenderDoc Python module must match the Python version used to build the
local RenderDoc package. The renderer build must not depend on this module.

## Descriptor Report Names

Reports should name N-Ray bindings in domain terms instead of only exposing raw
binding numbers:

| Binding | Type | N-Ray name |
| --- | --- | --- |
| 0 | storage buffer | pixel readback |
| 1 | storage buffer | triangle intersection SSBO |
| 2 | storage buffer | triangle shading SSBO |
| 3 | storage buffer | material SSBO |
| 4 | storage buffer | BVH SSBO |
| 5 | storage buffer | HDR accumulation buffer |
| 6 | storage buffer | render settings |
| 7 | combined image sampler array | glTF textures |
| 8 | storage buffer | shadow map |
| 9 | storage image | resolved HDR |
| 10 | storage image | normal/roughness |
| 11 | storage image | albedo/metallic |
| 12 | storage image | depth |
| 13 | storage image | material id |
| 14 | storage image | instance id |
| 15 | storage image | denoiser ping |
| 16 | storage image | denoiser pong |

Bindings `0..8` are the established preview contract. Denoiser storage-image
bindings `9..16` are appended and should remain appended unless the shader
contract is explicitly redesigned.

## Failure Modes

- **No Vulkan device:** fail the run with `VulkanComputePreview::statusMessage()`
  and no capture attempt.
- **RenderDoc API unavailable:** print that the binary must be launched by
  `renderdoccmd capture`, qrenderdoc, or injected RenderDoc.
- **RenderDoc layer did not attach:** capture API may be visible but
  `EndFrameCapture()` returns no saved file. Point users to the Vulkan layer
  registration guidance in `docs/renderdoc-vulkan-debugging.md`.
- **Python module unavailable:** replay/report script exits with setup guidance;
  the capture itself remains valid for qrenderdoc.
- **Model missing or invalid:** report the selected manifest entry or transient
  folder and the glTF importer status.
- **Capture contains OpenGL/ImGui only:** the wrong target was captured; use the
  headless tool or the in-app `Capture Next Vulkan Dispatch` action.
- **Unexpected `project_settings.json` diff:** transient `--model-folder` must
  not call the persistent import path.

## Validation

Documentation-only changes do not require a renderer build. For the future
implementation pass, use this minimum validation:

```bash
make build-renderdoc-headless
bin/Release/NrayRenderDocHeadless --help
timeout 45 renderdoccmd capture \
  --wait-for-exit \
  --working-dir /home/thadeu/projects/N-Ray/PathTracingRenderer \
  --capture-file /home/thadeu/projects/N-Ray/build/renderdoc/nray_headless \
  /home/thadeu/projects/N-Ray/bin/Release/NrayRenderDocHeadless \
    --model-index 0 --samples 1 --capture \
    --json-out /home/thadeu/projects/N-Ray/build/renderdoc/run.json
python3 tools/renderdoc_capture_report.py \
  /home/thadeu/projects/N-Ray/build/renderdoc/*.rdc \
  --json-out /home/thadeu/projects/N-Ray/build/renderdoc/report.json
```

If RenderDoc, Vulkan, or the Python replay module is unavailable on the current
machine, record the exact blocker output and still validate `--help` and argument
errors for the headless target.

Before committing an implementation, verify:

- `PathTracingRenderer/project_settings.json` is unchanged unless persistence
  was explicitly requested.
- Captures contain `vkQueueSubmit` and labeled compute regions such as
  `Main Compute Dispatch: glTF Model Preview`.
- Reports include descriptor access for the expected N-Ray bindings.
- The normal windowed app still builds and the existing menu-triggered RenderDoc
  capture path still works.
