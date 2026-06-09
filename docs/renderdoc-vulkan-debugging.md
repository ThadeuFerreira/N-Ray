# RenderDoc Vulkan Debugging

RenderDoc is a free MIT-licensed standalone graphics debugger with Vulkan
support, and it is the preferred manual frame debugger for N-Ray's Vulkan
preview. Use it when the renderer needs GPU-side evidence: descriptor bindings,
storage buffers, storage images, texture resources, compute dispatch order, or
shader inputs at a captured frame.

RenderDoc complements validation layers, logs, screenshots, and local builds. It
does not replace compile/link validation, and it does not turn visual inspection
into an automated test.

## Mental Model

RenderDoc works as an API wrapper around the target graphics process. During the
run it records resource creation/destruction, relevant uploads, and then, for the
captured frame, serializes API calls in order together with the initial resource
contents and states required by that frame.

When a capture is opened, RenderDoc replays the recorded API stream. The Event
Browser and API Inspector are built from an analysis pass over the captured
actions and resource dependencies. Most viewer interactions replay either the
frame up to the selected event, the frame up to just before that event, or the
selected event itself.

Implications for N-Ray debugging:

- A capture is a single-frame API/resource snapshot, not a live debugger.
- Resources that are not referenced by the captured frame may be absent.
- Texture and buffer contents reflect replayed frame state at the selected event.
- If an issue depends on progressive accumulation over time, capture the frame
  where the bad state is already visible and compare the resources that feed that
  frame.

## Launch N-Ray From RenderDoc

Use RenderDoc's `File -> Launch Application` flow. Prefer absolute paths for the
executable field.

- Executable: `/home/thadeu/projects/N-Ray/bin/Release/PathTracingRenderer`
- Alternate executable for symbols: `/home/thadeu/projects/N-Ray/bin/Debug/PathTracingRenderer`
- Shader-debug executable: `/home/thadeu/projects/N-Ray/bin/Debug/PathTracingRenderer`
- Working directory: `/home/thadeu/projects/N-Ray/PathTracingRenderer`
- Command line: normally empty
- Capture key: `F12` or `Print Screen`

The working directory is important. N-Ray loads runtime assets and settings from
relative paths, so captures should match `make run`, which launches from
`PathTracingRenderer/`. Launching from the repo root can make `models/`,
`textures/`, and `project_settings.json` resolve differently.

Use a RenderDoc build that matches the process bitness. N-Ray is built as a
64-bit application in the normal Linux and Visual Studio flows, so use a 64-bit
RenderDoc build.

## Shader-Debug Build

Normal `make` builds use the checked-in lean SPIR-V headers. For shader source
stepping in RenderDoc, use the opt-in debug shader path:

```bash
make shader-debug-headers
make build-shader-debug
```

`make shader-debug-headers` compiles every `PathTracingRenderer/shaders/*.comp`
with `glslangValidator -e main -gVS -V --target-env vulkan1.2`, validates each
module with `spirv-val --target-env vulkan1.2`, writes headers under
`build/generated/vulkan_shader_debug/`, and leaves the `.spv` files beside them
for `spirv-dis` and RenderDoc inspection. These generated files live under
`build/` and are not checked in.

`make build-shader-debug` regenerates Premake files with
`NRAY_VULKAN_SHADER_DEBUG=1` and builds `debug_x64` against the generated debug
headers. `make run-shader-debug` launches that binary from
`PathTracingRenderer/`.

The shader-debug runtime requires a Vulkan 1.2-capable device and either
`VK_KHR_shader_non_semantic_info` or Vulkan 1.3 support so embedded source/debug
information is legal. If that support is missing, the Vulkan preview reports a
clear initialization failure instead of silently falling back to lean shaders.

To confirm a debug module contains source/debug info:

```bash
spirv-dis build/generated/vulkan_shader_debug/vulkan_gltf_flat.comp.spv | \
  rg "NonSemantic.Shader.DebugInfo.100|vulkan_gltf_flat.comp"
```

## Vulkan Capture Setup

RenderDoc captures Vulkan through its Vulkan capture layer. A normal installer
usually registers that layer automatically. On Linux, the Vulkan loader discovers
implicit layers from locations such as:

- `/usr/share/vulkan/implicit_layer.d`
- `/etc/vulkan/implicit_layer.d`
- `$HOME/.local/share/vulkan/implicit_layer.d`

When launching from RenderDoc's capture dialog, RenderDoc sets up the target
process so the Vulkan loader initializes the capture layer. If the capture panel
warns that the Vulkan layer is not registered, use that warning/action first
before debugging N-Ray. A missing or unregistered layer can look like a renderer
problem because the app may launch without the RenderDoc overlay or capture
support.

N-Ray's current executable is a hybrid process: raylib/rlImGui presents the
visible window through OpenGL, while the Vulkan path-tracing preview is offscreen
compute followed by host readback. In that setup, RenderDoc's normal capture key
can select the presented OpenGL frame and show no Vulkan dispatches. For Vulkan
compute debugging, use N-Ray's in-app RenderDoc trigger instead:

- Launch N-Ray from RenderDoc or inject RenderDoc into the running process.
- Switch to Vulkan Mode and set up the model/shader state you want.
- Use `Vulkan -> Capture Next Vulkan Dispatch` in the app menu.
- Open the capture that RenderDoc saves after the next compute submit.

That trigger uses RenderDoc's in-application API and targets the Vulkan instance
with `RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance)`, while passing a null
window handle for the headless Vulkan work. It starts the capture before command
buffer reset/recording, submits the Vulkan compute command buffer, waits for the
fence, then ends the capture. This avoids the ambiguous `StartFrameCapture(NULL,
NULL)` case in a mixed OpenGL/Vulkan process.

For a one-shot capture without using the menu, set `NRAY_RENDERDOC_CAPTURE=1` in
the launched process environment. This arms the first Vulkan preview dispatch
after initialization. The menu command is usually better when you need to select
a specific model, camera position, denoiser state, or material first.

RenderDoc's Vulkan support page lists support for Vulkan 1.4 and a number of
extensions, but support still has caveats and not every replay feature is
available. For version- or extension-specific debugging, check the official
RenderDoc Vulkan support page before assuming a capture limitation is an N-Ray
bug.

## Vulkan Performance And Replay Notes

RenderDoc tries to keep non-capturing Vulkan overhead low, but a capture still
needs copies of frame resources and serialized API state. If captures are slow,
huge, or slow to replay:

- Prefer a small N-Ray viewport/resolution and a focused validation model.
- Avoid captures that include unnecessarily large GPU allocations.
- Be careful with coherent memory that stays persistently mapped. If a future
  Vulkan resource path becomes expensive to capture, mapping/unmapping around
  updates or reducing submits while coherent memory is persistently mapped can
  make RenderDoc captures cheaper.
- Avoid monolithic allocations around 1 GB or larger when capture ergonomics
  matter. RenderDoc may need one or more copies of allocations to replay the
  frame accurately.

RenderDoc's Vulkan replay is not generally portable across arbitrary machines.
Expect captures to replay on the same or a sufficiently similar system. Its
Vulkan support also assumes the captured app creates and uses one `VkDevice`.
Keep that in mind if future N-Ray work adds explicit multi-GPU support.

## Capture Workflow

1. Build the renderer first if needed. For docs-only work, avoid unnecessary
   builds because the raylib HDR setup can leave `vendor/raylib` dirty.
2. Launch the executable through RenderDoc with the working directory above.
3. In the app, switch to Vulkan Mode and select or import the target glTF model.
4. Let progressive accumulation reach a stable enough frame for the bug.
5. Use `Vulkan -> Capture Next Vulkan Dispatch` to arm the in-app Vulkan capture.
   RenderDoc will save the next offscreen Vulkan compute submit.
6. Open the capture and start from the Event Browser. For the compute preview,
   focus on dispatch events rather than graphics draw calls.

When investigating a bug, keep the reproduction small: model path, selected
Vulkan mode, shadow mode, denoiser mode/debug view, camera position if relevant,
material name/index, and the expected versus observed image behavior.

## Headless Agent Workflow

`NrayRenderDocHeadless` is the agent-friendly capture tool. It drives
`VulkanComputePreview` directly with no raylib window, no ImGui, and no global
app state, captures a deterministic Vulkan compute dispatch via RenderDoc's
in-process API, and emits JSON metadata and an optional PPM image.

**Usage**

```bash
# Quick render of a manifest model, no capture (verify the pipeline is alive)
cd PathTracingRenderer
../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --samples 4 \
  --json-out /tmp/run.json --ppm-out /tmp/out.ppm

# Full capture via renderdoccmd (working-dir must resolve models/ and project_settings.json)
mkdir -p ../build/renderdoc
renderdoccmd capture \
  --wait-for-exit \
  --working-dir /home/thadeu/projects/N-Ray/PathTracingRenderer \
  --capture-file /home/thadeu/projects/N-Ray/build/renderdoc/nray_headless \
  /home/thadeu/projects/N-Ray/bin/Release/NrayRenderDocHeadless \
    --model-index 0 --samples 1 --capture \
    --json-out /tmp/capture_run.json

# Transient folder (not persisted to project_settings.json)
../bin/Release/NrayRenderDocHeadless \
  --model-folder ../assets/beretta_m9_gameready \
  --samples 2 --json-out /tmp/transient.json

# Denoiser debug view
../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --samples 8 \
  --denoiser spatial-atrous --debug-view denoised \
  --json-out /tmp/denoised.json --ppm-out /tmp/denoised.ppm

# Lighting performance A/B
../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --width 128 --height 128 --samples 1 --max-bounces 3 \
  --shadow ray-traced --lighting-log --json-out /tmp/nray_light_base.json

../bin/Release/NrayRenderDocHeadless \
  --model-index 0 --width 128 --height 128 --samples 1 --max-bounces 3 \
  --shadow ray-traced --point-light-shadows --lighting-log \
  --json-out /tmp/nray_light_point_shadows.json
```

The `--help` flag prints all options with valid enum values.

**Verified behavior** (tested on Intel Iris Xe, Vulkan, Linux):

- Default procedural scene (no model), model-index, and model-folder paths all
  work and exit 0.
- `--capture` arms the in-process RenderDoc API. When launched under
  `renderdoccmd capture`, the `.rdc` file is created and its path appears in the
  JSON `captures` array. A 1-sample capture of the Nissan S15 model produces a
  ~33 MB `.rdc` on a 512×512 render.
- `project_settings.json` is never modified. The headless binary calls
  `preview.setPersistSettings(false)` before initialization, which gates both
  `saveSelectedModelToSettings` and `saveModelEntriesToSettings`.
- `--model-index` loads from the manifest but does not update
  `lastSelectedModelFolder`. `--model-folder` resolves, imports transiently, and
  loads without touching the manifest.
- If `--model-folder` points to an already-registered path in the manifest, the
  existing entry is reused (logged as "Using existing model folder:").
- Startup loads `lastSelectedModelFolder` from `project_settings.json`, which
  can cause a model to load and then be immediately replaced by `--model-index`.
  This is normal; the wasted load takes ~1 s for large models.
- Lighting options are included in the JSON `lighting` object and mirrored by
  `[VulkanLighting]` logs when `--lighting-log` is passed. Keep
  `--point-light-shadows` as an explicit A/B switch; the default path should not
  pay finite-distance BVH shadow rays for key/fill/rim lights.
- The RenderDoc Python module (`renderdoc`) is not in the Arch Linux package and
  is not available at the system level. The `tools/renderdoc_capture_report.py`
  script detects this and exits with clear setup guidance. The `.rdc` file is
  valid and can be opened in `qrenderdoc` for manual inspection regardless.

**JSON output schema** (all fields present on success):

```json
{
  "success": true,
  "width": 512, "height": 512,
  "shaderIndex": 5, "shaderName": "glTF Model Preview",
  "modelIndex": 0, "modelName": "Nissan S15 Silvia",
  "samplesRequested": 1, "samplesAccumulated": 1,
  "converged": true,
  "gpuDispatchMs": 15.26, "primaryRaysPerSec": 1.72e+07,
  "lighting": {
    "environment": true, "threePoint": true,
    "pointLightShadows": false,
    "directDiffuse": true, "directSpecular": true,
    "clearcoatSpecular": true,
    "specularScale": 1.0, "pointLightSize": 0.25
  },
  "sceneCounts": {
    "triangles": 1000, "bvhNodes": 511,
    "materials": 8, "textures": 12
  },
  "status": "...",
  "captures": ["/path/to/nray_headless_capture.rdc"]
}
```

`captures` is `[]` when `--capture` was not passed or RenderDoc was not
injected. `status` reflects the last status message from
`VulkanComputePreview::statusMessage()`. `lighting.pointLightShadows` should be
false for the default performance baseline and true only for the explicit
point-shadow comparison run.

**Offline report script**

`tools/renderdoc_capture_report.py` performs offline replay analysis and prints
compute dispatch counts, dispatch dimensions, and descriptor access for the first
dispatch. Requires the `renderdoc` Python module from inside `qrenderdoc`'s
embedded console or a custom RenderDoc build with Python bindings:

```bash
# From inside qrenderdoc's embedded Python console:
exec(open("tools/renderdoc_capture_report.py").read())
# Or from the shell if the module is available:
python3 tools/renderdoc_capture_report.py build/renderdoc/nray_headless_capture.rdc \
  --verbose --json-out /tmp/report.json
```

Manual qrenderdoc captures remain the right tool for interactive camera setup,
shader stepping, and visual inspection. The headless capture/report flow is for
fast evidence collection: capture the compute dispatch, summarize actions and
descriptor access with RenderDoc's Python API when available, and attach the
JSON report to the debugging notes.

## What To Inspect

- **Event Browser:** Find the Vulkan compute dispatches for the preview,
  denoiser passes, shadow-map prepass, and presentation/readback flow. Bookmark
  the events that produce or consume the resource under investigation.
  Current command-buffer regions include `Accumulation Clear`,
  `Accumulation Sample Barrier`, `Shadow Map Prepass`,
  `Main Compute Dispatch: <shader name>`, `Denoiser Prepare`,
  `Denoiser Atrous Pass <n>`, `Denoiser Composite`,
  `Immediate Submit: texture upload`, and `Final Host Readback Barrier`.
- **Pipeline State:** Confirm the active compute shader, descriptor set layout,
  and bound resources. The glTF path should point at `vulkan_gltf_flat.comp` for
  the main path-tracing dispatch.
- **API Inspector:** Check exact Vulkan calls around resource transitions,
  descriptor updates, dispatch dimensions, copies, and synchronization when a
  resource looks stale or unbound.
- **Texture Viewer:** Inspect output images, guide buffers, normal/albedo/depth
  views, shadow maps, and sampled glTF textures. Use the range controls for HDR
  or accumulation resources whose useful values are outside `[0, 1]`.
- **Buffer viewers:** Inspect SSBOs for BVH nodes, triangle records, material
  data, texture indices, volume/refraction fields, and denoiser parameters.
- **Resource names:** Vulkan buffers, descriptor objects, shader modules,
  pipelines, command resources, glTF texture images/views/samplers, and denoiser
  guide/intermediate images are named through `VK_EXT_debug_utils` when that
  instance extension is available. If RenderDoc shows raw handles only, first
  confirm the extension is exposed on the capture machine.
- **Timeline Bar:** Follow when a selected resource is written, copied, sampled,
  or displayed. This is useful for stale-frame, wrong-resource, and missing-copy
  bugs.

## Compute Shader Source Stepping

Use `make build-shader-debug` before launching from RenderDoc when the goal is
shader stepping or source-level inspection. Capture a frame, select a labeled
compute dispatch such as `Main Compute Dispatch: glTF Model Preview`, then open
RenderDoc's shader viewer/debugger from the Pipeline State or event context. The
debug build embeds GLSL source through non-semantic SPIR-V debug information, so
the shader debugger should show source lines for current software BVH compute
shaders such as `vulkan_gltf_flat.comp`.

If source is missing, check that the captured executable is
`bin/Debug/PathTracingRenderer` from `make build-shader-debug`, not a normal
Debug build generated without `NRAY_VULKAN_SHADER_DEBUG=1`. Also check the
disassembly command above for the expected `NonSemantic.Shader.DebugInfo.100`
import and source filename.

## N-Ray Debug Targets

### glTF Materials And Textures

Use RenderDoc when a model loads but appears visually wrong. Inspect descriptor
arrays and material buffers before changing lighting. Verify that base color,
metallic-roughness, normal, emissive, transmission, and thickness texture indices
match the material you selected in the Vulkan material panel.

### Transparency And Refraction

For glass issues, capture the frame with the target material selected. Inspect
the material SSBO fields for transmission factor, IOR, optical mode, volume
thickness, attenuation color, attenuation distance, and texture indices.

Current status: thin transmission is working in the Vulkan compute preview.
Volume refraction is wired but still in visual tuning. A RenderDoc capture should
help answer whether the problem is bad material data, missing texture bindings,
unexpected fallback thickness, or shader behavior after the correct data is
bound.

### Shadows

For shadow bugs, capture once in `Ray Traced` mode and once in `Shadow Map` mode
when possible. Ray-traced shadows attenuate through transmissive materials;
shadow-map mode remains approximate for transparent and volumetric casters.
Inspect the shadow resources and the main dispatch input state before assuming a
lighting equation regression.

### Denoising

For denoiser bugs, inspect the prepare, atrous, and composite dispatches. Check
raw accumulation, albedo, normal, depth, material id, ping-pong, and final
denoised resources. If a debug view looks wrong, verify the guide buffer that
feeds it before changing filter weights.

## Ray Tracing Pipeline Limitations

The guidance above applies to N-Ray's current Vulkan compute/BVH preview. That
path is normal Vulkan compute work, so RenderDoc can inspect dispatches,
descriptors, storage buffers, storage images, and textures.

This also applies to future compute shaders that use `GL_EXT_ray_query`, because
they still execute as ordinary compute dispatches. RenderDoc does not support
introspecting Vulkan or D3D12 ray-tracing pipeline work. Future
`VK_KHR_ray_tracing_pipeline` dispatches should be treated as opaque in captures:
RenderDoc can record the calls and replay their effects so later normal graphics
or compute work sees correct resource contents, but it cannot debug the
ray-tracing bindings, shader tables, raygen/miss/hit shaders, payloads,
acceleration-structure traversal, or the ray-tracing work itself.

Do not plan HWRT debugging around RenderDoc shader or binding inspection. Use it
only to inspect resources before/after ray-tracing work and any non-ray-tracing
passes that consume those results. On NVIDIA Vulkan drivers, RenderDoc may not be
able to capture Vulkan ray-tracing pipeline work at all; ray query work is the
supported ray-tracing-adjacent path to expect in captures.

## Notes And Limits

- RenderDoc captures one frame. It is best for resource state and per-frame
  pipeline inspection, not for proving long-running convergence behavior.
- Keep Vulkan validation layers available for API misuse and synchronization
  errors; use RenderDoc when you need to see what the GPU resources contained.
- Hardware ray-tracing pipeline work is a RenderDoc black box; debug the
  surrounding resource flow and non-ray-tracing passes instead.
- If RenderDoc itself fails or captures incorrectly, reduce the reproduction and
  check the RenderDoc FAQ or issue tracker before assuming the renderer changed.
- If a capture contains OpenGL/ImGui events but no `vkQueueSubmit` or
  `vkCmdDispatch`, it is probably the presented raylib frame, not the Vulkan
  compute preview. Use the in-app Vulkan capture trigger above.
- If the in-app trigger reports that the capture ended without a saved capture,
  check RenderDoc's diagnostics for Vulkan capture-layer initialization. The
  app's RenderDoc API was visible, but the Vulkan layer likely did not attach to
  the `VkInstance`/`VkDevice`.
- Official RenderDoc documentation and tutorials are available from
  <https://renderdoc.org/docs/>.
- Do not infer a graphics-pipeline architecture from RenderDoc's UI. N-Ray's
  current glTF preview is compute/BVH-based; graphics draw events are mostly
  display/UI plumbing around the compute path.
