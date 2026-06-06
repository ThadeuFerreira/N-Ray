---
name: nray-vulkan-tutorials
description: Use when working on Vulkan issues in the N-Ray repository, especially compute shader experiments, Vulkan hello-world/window bring-up, VulkanCore wrapper usage, synchronization barriers, descriptor sets, shader compilation, glTF asset import/conversion/skinning/animation reference examples, Vulkan PBR pipelines (material push constants, IBL pre-computation, irradiance/BRDF-LUT/prefiltered-cube descriptor layouts, textured PBR with tangent vertex attributes), or porting CPU path tracing concepts toward Vulkan. Always check the local ogldev tutorial tree under /home/thadeu/projects/N-Ray/tutorials/ogldev/Vulkan, the vendored glTF loaders under /home/thadeu/projects/N-Ray/tutorials/saschawillems/gltf, and the upstream Sascha Willems gltfskinning guide before designing Vulkan or glTF code from scratch.
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
- `tutorials/saschawillems/gltf/` - canonical local glTF-loading references (vendored `SaschaWillems/Vulkan` `gltfloading` + `gltfscenerendering`): tinygltf accessor decode, node/TRS hierarchy traversal, uint8/16/32 index handling, `TANGENT`/TBN, and per-material handling. Reference-only/not built; see its `README.md` for the technique-to-`gltf_scene.cpp` map.
- Upstream `SaschaWillems/Vulkan/examples/gltfskinning` - use this as the skinned-animation reference when adding real joint palettes: mutable TRS nodes, `JOINTS_0`/`WEIGHTS_0`, `Skin` inverse bind matrices, animation samplers/channels, per-frame joint-matrix SSBOs, `updateAnimation`, `updateJoints`, and shader-side weighted skin matrices. This sample is documented in `docs/gltf-vulkan-pbr-import.md`; it is not currently vendored in-tree.
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
- `assets/2024_lbsilhouette_works_murcielago_gt_evo/scene.gltf`
- `assets/accurate_torvosaurus_tanneri/scene.gltf`
- `assets/beretta_arx160/scene.gltf`
- `assets/beretta_m9_gameready/scene.gltf`
- `assets/hulk_infinity_hulk/scene.gltf`
- `assets/luna_snow_-_sonic_trailblazer/scene.gltf`
- `assets/wolverine_-_wolverine_-_x-2099_bundle/scene.gltf`

Do not use `PathTracingRenderer/models/` or `PathTracingRenderer/textures/` as
the glTF validation source; those are the current OBJ/HDRI runtime assets for
the CPU scene. If an `assets/` subfolder lacks a glTF/GLB scene file, treat it as
source material only until an importable scene entry point is added.

When writing or extending the glTF importer
(`PathTracingRenderer/src/gltf_scene.cpp`), grep the vendored loaders under
`tutorials/saschawillems/gltf/` for a proven conversion pattern before designing
from scratch, and follow the channel/color-space/tangent rules in
`docs/gltf-vulkan-pbr-import.md`.

## glTF Skinning Guidance

The current Vulkan glTF preview only bakes skinned meshes into a static bind pose
on the CPU before BVH build. When moving beyond that bridge into animated
skinning, use the upstream Sascha Willems `gltfskinning` sample and the Khronos
skin tutorial as the reference model:

- Keep node hierarchy and mutable TRS components. Animation channels update
  translation, rotation, or scale independently, so do not collapse node state
  permanently at load time.
- Decode `JOINTS_0` and `WEIGHTS_0` as first-class vertex attributes. glTF base
  skinning supports four joint influences; handle uint8/uint16/uint32 joints and
  float or normalized integer weights deliberately.
- Store each `Skin` as a skeleton root, joint-node list, inverse bind matrices,
  and a per-skin joint-matrix upload buffer. The graphics sample binds this as
  an SSBO; the N-Ray compute path should represent it as explicit scene data.
- Implement animation samplers/channels in stages. Start with `LINEAR`
  interpolation, using linear mix for translation/scale and quaternion slerp for
  rotation. Treat morph-target `weights` as separate future work.
- Build runtime joint matrices as
  `inverse(meshNodeGlobal) * jointGlobal * inverseBindMatrix` for shader use. The
  CPU bind-pose importer currently flattens world-space triangles instead, so it
  intentionally uses a different space.
- Watch the root-axis correction path. Static meshes naturally inherit glTF
  scene root transforms, but skinned meshes can appear 90 degrees wrong if the
  skeleton palette does not include the equivalent root/global correction.

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

## HDR Rendering Pipeline Patterns (SaschaWillems hdr example)

These patterns come from the `SaschaWillems/Vulkan` HDR sample and are directly relevant to porting N-Ray's accumulation/tone-mapping pipeline to Vulkan.

### Multi-pass render graph: offscreen → bloom filter → composition

Three render passes in sequence:
1. **Offscreen G-buffer pass** — renders scene to two `VK_FORMAT_R32G32B32A32_SFLOAT` color attachments + depth. Fragment shader writes HDR color to `color[0]` and bright-only (bloom threshold) to `color[1]`. Both end with `finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` so they are ready to sample in later passes with no explicit barrier.
2. **Bloom filter pass** — fullscreen triangle reads `color[0]`/`color[1]` from offscreen, writes blurred result to `filterPass.color[0]` (also `R32G32B32A32_SFLOAT`). Uses a pair of `VK_SUBPASS_EXTERNAL` dependencies to drive the layout transition implicitly.
3. **Composition pass** — fullscreen triangle samples `offscreen.color[0]` (HDR scene) and `filterPass.color[0]` (blurred bloom), tone-maps with exposure, and presents to the swapchain framebuffer. Bloom is composited with additive blending on a second draw.

**N-Ray mapping:** The GPU path tracer's `accumBuffer → composeRenderFrame → publishedFrame` pipeline maps directly onto offscreen storage image → composition pass → swapchain present. The bloom pass is optional post-processing on top.

### FrameBufferAttachment helper struct

```cpp
struct FrameBufferAttachment {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    VkFormat format;
    void destroy(VkDevice device) {
        vkDestroyImageView(device, view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, mem, nullptr);
    }
};
```

`createAttachment(format, usageBit, &attachment)` wraps `vkCreateImage` + `vkAllocateMemory` + `vkBindImageMemory` + `vkCreateImageView` in one call. It always appends `VK_IMAGE_USAGE_SAMPLED_BIT` alongside the requested usage so the attachment can be sampled in subsequent passes. Aspect mask logic (color vs. depth+stencil) lives here, not at the call site.

### Fullscreen triangle (no vertex buffer)

```cpp
VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
pipelineCI.pVertexInputState = &emptyInputState;
// ...
vkCmdDraw(cmdBuffer, 3, 1, 0, 0);  // vertex shader generates positions from gl_VertexIndex
```

The vertex shader reconstructs clip-space positions from `gl_VertexIndex` (0, 1, 2) and derives UVs without any bound VBO. Use this for all fullscreen passes (composition, bloom, tone-mapping). It avoids a vertex buffer upload and matches how N-Ray's CPU `composeRenderFrame` works — a pure image operation with no geometry.

### Specialization constants for shader variants

```cpp
VkSpecializationMapEntry entry = { .constantID=0, .offset=0, .size=sizeof(uint32_t) };
uint32_t dir = 1;  // 1 = vertical, 0 = horizontal
VkSpecializationInfo spec = { .mapEntryCount=1, .pMapEntries=&entry,
                              .dataSize=sizeof(dir), .pData=&dir };
shaderStages[1].pSpecializationInfo = &spec;
// one more vkCreateGraphicsPipelines with dir=0 yields the second bloom pass
```

Creates `pipelines.bloom[0]` and `pipelines.bloom[1]` (two blur directions) and `pipelines.skybox`/`pipelines.reflect` (two shading modes) from the same SPIR-V. Prefer this over separate `.spv` files when only a single integer or bool differs between pipeline variants.

### Descriptor sets per frame; images shared

```cpp
std::array<DescriptorSets, maxConcurrentFrames> descriptorSets{};
std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;
// Each frame: its own UBO descriptor → its own buffer.
// All frames: same VkImageView descriptors (attachments not in flight simultaneously).
```

Uniform buffers must be per-frame to avoid write-after-read hazards on `memcpy`. Sampler/image descriptors reference attachments that are not simultaneously in flight across frames, so they can be shared (or duplicated for simplicity). Allocate one `VkDescriptorSet` per frame for UBO-containing sets; reuse the same `VkDescriptorImageInfo` across all frames in corresponding writes.

### Subpass dependencies replace explicit barriers between passes

```cpp
// Transition offscreen color UNDEFINED → SHADER_READ_ONLY is expressed
// as finalLayout in VkAttachmentDescription, not vkCmdPipelineBarrier.
attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
// Plus srcSubpass=VK_SUBPASS_EXTERNAL→dstSubpass=0 and
//      srcSubpass=0→dstSubpass=VK_SUBPASS_EXTERNAL dependencies.
```

Prefer subpass dependencies over explicit `vkCmdPipelineBarrier` calls for attachment layout transitions between render passes. Reserve explicit barriers for compute→graphics or cross-queue handoffs that subpass dependencies cannot express.

### HDR offscreen sampler settings

Use `VK_FILTER_NEAREST` + `VK_SAMPLER_MIPMAP_MODE_LINEAR` + `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` for all offscreen attachment samplers. Nearest filtering avoids interpolation artifacts on full-resolution HDR float data. `CLAMP_TO_EDGE` prevents border-color bleed on bloom/blur edges.

### Additive bloom blend state

```cpp
blendAttachmentState.blendEnable = VK_TRUE;
blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
```

`ONE + ONE` additive blend for the bloom draw over the already-composited base. This is the correct mode for bloom — it accumulates light energy without darkening the base pass.

### Exposure and tone mapping via UBO

```cpp
struct UniformData {
    glm::mat4 projection, modelview, inverseModelview;
    float exposure{ 1.0f };
} uniformData;
```

Exposure is a per-frame scalar in the host-visible coherent UBO. The composition fragment shader applies it before the tone-mapping operator. **N-Ray mapping:** the existing `params.exposure` + S-curve contrast + gamma 2.2 inside `composeRenderFrame` translate directly — wrap them in a small push-constant or UBO block passed each frame to the composition pipeline.

## Vulkan PBR Reference (SaschaWillems pbrbasic / pbribl / pbrtexture)

Three progressive examples cover the full Vulkan PBR stack — analytical-only lights up through IBL with per-mesh texture maps. Use these patterns when porting N-Ray's `PBRMaterial` pipeline to Vulkan.

### pbrbasic — analytical lights, material via push constants

Material parameters travel as push constants (not a UBO) so each draw call in the grid sets its own roughness/metallic/color cheaply:

```cpp
struct Material::PushBlock { float roughness, metallic, r, g, b; };
// Two push constant ranges: vertex gets glm::vec3 position, fragment gets PushBlock
pushConstantRanges = {
    pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT,   sizeof(glm::vec3),           0),
    pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Material::PushBlock), sizeof(glm::vec3)),
};
// Per-cell draw in 7×7 grid — vary metallic (x axis) and roughness (y axis):
vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,   0,                sizeof(glm::vec3),           &pos);
vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::vec3), sizeof(Material::PushBlock), &mat);
models.objects[idx].draw(cmd);
```

Descriptor layout: binding 0 = matrices UBO (vertex+fragment), binding 1 = params/lights UBO (fragment). Four analytical point lights in `UniformDataParams.lights[4]`; two orbit per-frame, two stay static.

**N-Ray mapping:** `PBRMaterial`'s per-material roughness/metalness/albedo can start as push constants for the first Vulkan pass before texture map support is added.

### IBL pre-computation (pbribl / pbrtexture)

Three textures baked once at startup from an HDR environment cube map. All share the same offscreen-render-then-copy pattern.

**BRDF LUT** (`generateBRDFLUT`) — stores (scale, bias) split-sum approximation for the specular integral:
- Format: `VK_FORMAT_R16G16_SFLOAT`, 512×512, 1 mip, clamp-to-edge sampler
- Rendered with a fullscreen triangle (empty vertex input, no descriptor set)
- Shaders: `genbrdflut.vert` + `genbrdflut.frag`
- `finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` in the attachment description (no explicit barrier needed)

**Irradiance cube** (`generateIrradianceCube`) — diffuse irradiance for the ambient term:
- Format: `VK_FORMAT_R32G32B32A32_SFLOAT`, 64×64, 6 layers × `numMips = floor(log2(64))+1 = 7`
- Per-face push constant: `{ glm::mat4 mvp; float deltaPhi = 2π/180; float deltaTheta = 0.5π/64; }`
- Shaders: `filtercube.vert` + `irradiancecube.frag`

**Prefiltered environment cube** (`generatePrefilteredCube`) — specular reflection at varying roughness:
- Format: `VK_FORMAT_R16G16B16A16_SFLOAT`, 512×512, 6 layers × `numMips = floor(log2(512))+1 = 10`
- Per-face push constant: `{ glm::mat4 mvp; float roughness; uint32_t numSamples = 32; }`
- `roughness = (float)mipLevel / (float)(numMips - 1)` — rougher mips at higher mip levels
- Shaders: `filtercube.vert` + `prefilterenvmap.frag`

**Shared offscreen-render-and-copy loop** (irradiance + prefiltered cubes):
1. Create temporary 2D offscreen color attachment (`dim×dim`, same format, `COLOR_ATTACHMENT_BIT | TRANSFER_SRC_BIT`)
2. Transition cube target to `TRANSFER_DST_OPTIMAL` once via `vks::tools::setImageLayout`
3. For each mip `m`, for each face `f`: set viewport to `dim * 0.5^m`, push `mvp = perspective(90°) * faceMatrix[f]`, draw the skybox cube, `vkCmdCopyImage` offscreen → cube face/mip
4. Transition cube to `SHADER_READ_ONLY_OPTIMAL`
5. Destroy offscreen image/view/memory/framebuffer after the single `flushCommandBuffer`

Face matrices (6 views from cube center): `glm::rotate` chains to aim at each ±X/±Y/±Z face with the appropriate flip.

### IBL PBR descriptor layout (pbribl scene descriptor set)

```
Binding 0: UNIFORM_BUFFER  (VERTEX | FRAGMENT) — UniformDataMatrices {proj, model, view, camPos}
Binding 1: UNIFORM_BUFFER  (FRAGMENT)          — UniformDataParams   {lights[4], exposure, gamma}
Binding 2: COMBINED_IMAGE_SAMPLER (FRAGMENT)   — irradianceCube
Binding 3: COMBINED_IMAGE_SAMPLER (FRAGMENT)   — lutBrdf
Binding 4: COMBINED_IMAGE_SAMPLER (FRAGMENT)   — prefilteredCube
```

The skybox uses a parallel descriptor set with the same layout but binding 2 = `environmentCube` (raw HDR cube, not the convolved irradiance). Material parameters are still push constants (same `PushBlock` as pbrbasic, with an added `specular` field).

### Textured PBR additions (pbrtexture)

Extends pbribl with per-mesh texture maps; the `PushBlock` is removed — all parameters come from textures:

```
Binding 5: albedoMap    — VK_FORMAT_R8G8B8A8_UNORM
Binding 6: normalMap    — VK_FORMAT_R8G8B8A8_UNORM
Binding 7: aoMap        — VK_FORMAT_R8_UNORM
Binding 8: metallicMap  — VK_FORMAT_R8_UNORM
Binding 9: roughnessMap — VK_FORMAT_R8_UNORM
```

Vertex attribute set expands to `Position, Normal, UV, Tangent` — **tangent is mandatory for normal mapping**. Load glTF with `PreTransformVertices | PreMultiplyVertexColors | FlipY`.

### Per-frame UBO / shared image pattern

All three examples share the same memory layout:

```cpp
std::array<UniformBuffers,  maxConcurrentFrames> uniformBuffers;  // per frame
std::array<DescriptorSets,  maxConcurrentFrames> descriptorSets;  // per frame
// Textures (irradiance, LUT, prefilteredCube, albedo, etc.) are NOT duplicated —
// each frame's descriptor set writes the same VkDescriptorImageInfo.
```

Two UBOs per frame (`scene` = matrices, `params` = lights/exposure/gamma), both `HOST_VISIBLE | HOST_COHERENT` with persistent `map()`. Update via `memcpy(buffer.mapped, &data, sizeof(data))`:

```cpp
// Skybox: strip translation so the cube stays centered on the camera
uniformDataMatrices.model = glm::mat4(glm::mat3(camera.matrices.view));
// camPos for specular: view is inverse camera transform
uniformDataMatrices.camPos = camera.position * -1.0f;
```

### N-Ray Vulkan PBR mapping

| N-Ray CPU concept | Vulkan PBR equivalent |
|---|---|
| `PBRMaterial.roughness / metalness` | push constants (pbrbasic first pass) → metallic/roughness texture maps (pbrtexture) |
| `PBRMaterial.albedo` | push constants (color r/g/b) → `albedoMap` |
| `hdriLogic` equirectangular HDRI | `environmentCube` → run `generateIrradianceCube` + `generatePrefilteredCube` at load; must convert equirectangular to cube first (or load `.ktx` cube directly) |
| `specularLighting` (GGX + Fresnel-Schlick) | same BRDF in fragment shader against `prefilteredCube` + `lutBrdf` split-sum lookup |
| `diffuseLighting` (cosine-weighted hemisphere) | `irradianceCube` lookup — replaces stochastic hemisphere sampling with a single texture fetch |
| `composeRenderFrame` (exposure + S-curve + gamma) | `UniformDataParams.exposure / gamma` → composition fragment shader |
| Stochastic environment sampling (path tracer) | For a GPU path tracer, sample `prefilteredCube` at the bounce direction to evaluate environment contribution without per-bounce hemisphere sampling; BRDF LUT replaces full Fresnel+GGX integration |

## Integration Guidance

- Keep Tutorial28 as a reference/smoke-test harness unless the user explicitly asks to wire its code into the main path tracer executable.
- Match the tutorial's descriptor contract for compute image generation: binding `0` is a `rgba8` storage image, binding `1` is a uniform buffer with `float time`.
- When adapting code to N-Ray proper, keep the CPU renderer's render-worker/data boundaries in mind and avoid coupling Vulkan experiments directly to the current raylib ImGui loop unless requested.
- Validate with build output and, when a window can be opened, state the exact shader argument used.
