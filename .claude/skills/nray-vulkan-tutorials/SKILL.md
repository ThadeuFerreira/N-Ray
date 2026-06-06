---
name: nray-vulkan-tutorials
description: Use when working on Vulkan issues in the N-Ray repository, especially compute shader experiments, Vulkan hello-world/window bring-up, VulkanCore wrapper usage, synchronization barriers, descriptor sets, shader compilation, glTF asset import/conversion reference examples, or porting CPU path tracing concepts toward Vulkan. Always check the local ogldev tutorial tree under /home/thadeu/projects/N-Ray/tutorials/ogldev/Vulkan, and the vendored glTF loaders under /home/thadeu/projects/N-Ray/tutorials/saschawillems/gltf, before designing Vulkan or glTF code from scratch.
---

# N-Ray Vulkan Tutorials

Use this skill for Vulkan-related work in `/home/thadeu/projects/N-Ray`.

## Reference First

Before designing Vulkan code from scratch, search the local tutorial tree:

```bash
rg -n "<keyword>" tutorials/ogldev/Vulkan tutorials/ogldev/Common
```

High-value references:

- `tutorials/ogldev/Vulkan/Tutorial28/` - compute shader texture generation, storage image output, uniform buffer time input, fullscreen quad display, and the ImGui "Hello, world!" Vulkan window.
- `tutorials/ogldev/Vulkan/VulkanCore/` - local Vulkan helper wrappers for instance/device/swapchain, queues, command buffers, shader compilation, descriptor pools, textures, graphics pipelines, compute pipelines, and ImGui integration.
- `tutorials/ogldev/Vulkan/Tutorial13` through `Tutorial27` - graphics pipeline, shader, texture, model, and swapchain progression before compute.
- `tutorials/ogldev/Vulkan/Tutorial29` and later - more advanced rendering/data-flow examples after compute.
- `tutorials/saschawillems/gltf/` - canonical local glTF-loading references (vendored `SaschaWillems/Vulkan` `gltfloading` + `gltfscenerendering`): tinygltf accessor decode, node/TRS hierarchy traversal, uint8/16/32 index handling, `TANGENT`/TBN, and per-material handling. Reference-only/not built; see its `README.md` for the technique→`gltf_scene.cpp` map.
- `docs/vulkan-migration-guide.md` - N-Ray-specific Vulkan migration direction and dependency choices.
- `docs/vulkan-compute-path-tracing-plan.md` - concrete compute-shader path tracing buffer contract, BVH upload strategy, and staged GPU migration plan.
- `assets/` - local glTF validation corpus for future importer, PBR material, texture color-space, tangent, and Vulkan upload work. Prefer `assets/*/scene.gltf` or `.glb` before external model downloads.
- `vendor/volk/` - Vulkan function loader only; it still includes Vulkan API headers such as `<vulkan/vulkan_core.h>` and does not replace `libvulkan-dev` or the LunarG SDK headers.
- `vendor/VulkanMemoryAllocator/include/vk_mem_alloc.h` - VMA also includes `<vulkan/vulkan.h>`, so it requires Vulkan headers too.

## glTF Validation Assets

Use top-level `assets/` as N-Ray's local model validation source for glTF,
Vulkan, and PBR import work. Importable validation bundles should expose
`assets/<name>/scene.gltf` or a `.glb` file and keep folder-local `scene.bin`,
`textures/`, and license/source files beside that scene file.

Current local glTF entry points:

- `assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf`
- `assets/accurate_torvosaurus_tanneri/scene.gltf`
- `assets/beretta_arx160/scene.gltf`
- `assets/beretta_m9_gameready/scene.gltf`

Do not use `PathTracingRenderer/models/` or `PathTracingRenderer/textures/` as
the glTF validation source; those are the current OBJ/HDRI runtime assets for
the CPU scene. If an `assets/` subfolder lacks a glTF/GLB scene file, treat it as
source material only until an importable scene entry point is added.

When writing or extending the glTF importer
(`PathTracingRenderer/src/gltf_scene.cpp`), grep the vendored loaders under
`tutorials/saschawillems/gltf/` for a proven conversion pattern before designing
from scratch, and follow the channel/color-space/tangent rules in
`docs/gltf-vulkan-pbr-import.md`.

## Compute Shader Smoke Test

For the Tutorial28 compute window:

```bash
cd /home/thadeu/projects/N-Ray/tutorials/ogldev/Vulkan/Tutorial28
./build.sh
./run.sh flux_core.comp
```

Any compatible `.comp` file in that folder can be passed as the first argument. Running requires a working display, Vulkan SDK, Vulkan validation/runtime libraries, and the tutorial's `../../Lib` dependencies.

For windowed smoke logs, prefer line buffering because `timeout` may kill the app before block-buffered stdout flushes:

```bash
timeout 20 stdbuf -oL -eL ./run.sh flux_core.comp > /tmp/nray_tutorial28.log 2>&1
rg -n "Using compute shader|Swap chain extent|Command buffers recorded|Error|error|failed" /tmp/nray_tutorial28.log
```

Exit `124` from `timeout` is expected when the GLFW window stays alive. Mesa device-select debug callback messages can be non-fatal if initialization continues.

## N-Ray Compute Migration Guardrails

The main N-Ray migration currently targets `VK_PIPELINE_BIND_POINT_COMPUTE`, not `VK_KHR_ray_tracing_pipeline`. Compute shaders do not have hardware triangle or vertex-input state; all scene data must be explicit descriptor-bound buffers/images.

Current N-Ray scene data is already renderer-owned, not primarily raylib `Mesh` data:

- `Data::tris` - fat triangle records for positions, normals, material/model ids, AABBs, and centers.
- `Data::triIsect` - compact intersection records used by traversal.
- `Data::materials` - `PBRMaterial` records edited by the UI.
- `globalCompactBVH` - flattened BVH nodes from `PathTracer::flattenBVH`.

For the first real GPU path, upload those existing arrays to SSBOs before redesigning asset loading. If a future raylib `Mesh`, Assimp, or glTF loader is added, flatten it into the same GPU upload structs rather than binding graphics vertex buffers directly to compute.

Recommended first pass:

- Use explicit `vec4`/`uvec4`-style GPU structs to avoid `std430`/`glm::vec3` alignment mistakes.
- Keep compact intersection data separate from shading/material data, mirroring the CPU `TriIntersect` optimization.
- Reuse `CompactBVH` semantics in the shader: leaf when `triCount > 0`, first child at `current + 1`, second child by index, fixed local traversal stack.
- Port `rayAABB`, `RayIntersectsTriangle`, and `traverseFlatBVH` before adding bounces or full PBR.
- Treat the current `VulkanComputePreview` host-visible pixel-buffer readback as a bridge only. The target renderer should write an accumulation storage image/buffer and eventually avoid CPU readback.

## ImGui Mouse Coordinate Debugging

When ImGui hover/click positions look wrong in Tutorial28 or VulkanCore:

- Check GLFW logical window size, GLFW framebuffer size, and the chosen swapchain extent. On high-DPI/scaled desktops these can differ; one observed case requested `1280 x 720` but created a `1600 x 900` swapchain.
- Treat GLFW mouse/input coordinates as logical window coordinates. Let `ImGui_ImplGlfw_NewFrame()` own `io.DisplaySize` and mouse state; do not seed `io.DisplaySize` from framebuffer pixels.
- Treat Vulkan render areas, depth images, graphics viewport/scissor, and fullscreen quad output as swapchain/render-target pixels. Store the selected `VkExtent2D` from swapchain creation and expose it as the render size.
- Before `ImGui_ImplVulkan_RenderDrawData`, set `ImDrawData::FramebufferScale` to `renderSize / drawData.DisplaySize` so ImGui draws and clips into the actual Vulkan render target while hit-testing remains in GLFW coordinates.
- Poll GLFW events before per-frame camera/UI updates so hover state and camera input use current cursor state.
- Disable `ImGuiConfigFlags_NavEnableSetMousePos` in this sample unless keyboard navigation intentionally needs OS cursor warping; it can obscure mouse-position debugging.

## Integration Guidance

- Keep Tutorial28 as a reference/smoke-test harness unless the user explicitly asks to wire its code into the main path tracer executable.
- Match the tutorial's descriptor contract for compute image generation: binding `0` is a `rgba8` storage image, binding `1` is a uniform buffer with `float time`.
- When adapting code to N-Ray proper, keep the CPU renderer's render-worker/data boundaries in mind and avoid coupling Vulkan experiments directly to the current raylib ImGui loop unless requested.
- Validate with build output and, when a window can be opened, state the exact shader argument used.
