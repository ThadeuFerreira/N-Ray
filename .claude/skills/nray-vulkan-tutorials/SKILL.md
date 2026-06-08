---
name: nray-vulkan-tutorials
description: Use when working on Vulkan issues in the N-Ray repository, especially compute shader experiments, Vulkan hello-world/window bring-up, VulkanCore wrapper usage, synchronization barriers, descriptor sets, shader compilation, glTF asset import/conversion/skinning/animation reference examples, Vulkan PBR pipelines (material push constants, IBL pre-computation, irradiance/BRDF-LUT/prefiltered-cube descriptor layouts, textured PBR with tangent vertex attributes), hardware ray tracing (VK_KHR_ray_tracing_pipeline, BLAS/TLAS build, SBT layout, raygen/miss/closesthit/anyhit/intersection/callable shader groups, frame accumulation, glTF ray tracing with descriptor indexing, recursive secondary rays for shadows and reflections via multiple miss shaders/ray payloads/iterate-in-raygen bounce loops), or porting CPU path tracing concepts toward Vulkan compute or HW ray tracing. Always check the local ogldev tutorial tree under /home/thadeu/projects/N-Ray/tutorials/ogldev/Vulkan, the vendored glTF loaders under /home/thadeu/projects/N-Ray/tutorials/saschawillems/gltf, and the upstream Sascha Willems gltfskinning and ray tracing example guides before designing Vulkan or glTF code from scratch. Scope is Vulkan-first: only touch CPU path-tracer internals when explicitly requested by the user.
---

# N-Ray Vulkan Tutorials

Use this skill for Vulkan-related work in `/home/thadeu/projects/N-Ray`.

## Scope policy

Vulkan work is the default and highest-priority path for this repository. Do not
edit `PathTracer` internals, CPU accumulation logic, or `AsyncRenderWorker` hot
paths unless the user explicitly commands CPU path-tracer work.

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
- Upstream `SaschaWillems/Vulkan/examples/raytracingbasic` — canonical HW RT bringup: extension + feature chain, function pointer loading, BLAS/TLAS build, storage image, SBT sizing, `vkCmdTraceRaysKHR`, and blit-to-swapchain. Use as the first reference before writing any `VK_KHR_ray_tracing_pipeline` code.
- Upstream `SaschaWillems/Vulkan/examples/raytracinggltf` — glTF ray tracing with multi-primitive BLAS, `GeometryNode` SSBO for per-primitive material/texture lookup, `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT` for unbounded texture arrays, any-hit shader for transparency, and frame-accumulation counter for progressive anti-aliasing.
- Upstream `SaschaWillems/Vulkan/examples/raytracingcallable` — callable shaders: multi-geometry BLAS (one geometry per object), `gl_GeometryIndexEXT` dispatch, separate callable SBT with one handle per geometry, and `executeCallableEXT`.
- Upstream `SaschaWillems/Vulkan/examples/raytracingintersection` — procedural geometry: `VK_GEOMETRY_TYPE_AABBS_KHR` BLAS, intersection shader (`.rint`) calling `reportIntersectionEXT`, `VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR`, and per-sphere SSBO indexed by `gl_PrimitiveID`.
- Upstream `SaschaWillems/Vulkan/examples/raytracingshadows` — recursive secondary rays: a second miss shader (`shadow.rmiss`) for shadow-ray occlusion, the TLAS bound to `VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR` (not just raygen) so closest-hit can launch `traceRayEXT`, vertex/index SSBOs for hit-attribute reconstruction, and `maxPipelineRayRecursionDepth = 2`. The foundation for any reflection/secondary-bounce work.
- Upstream `SaschaWillems/Vulkan/examples/raytracingreflections` — recursive reflections: the **iterate-in-raygen** bounce loop (closest-hit returns surface data via the payload; raygen reflects and re-traces in a loop) that keeps `maxPipelineRayRecursionDepth` low while still doing N reflection bounces. This is the direct analogue of N-Ray's `rayLogic` bounce loop — use it as the primary reflection reference.
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

## Vulkan Compute Preview Manifest

The Vulkan preview model list now persists in
`PathTracingRenderer/project_settings.json` instead of being hardcoded. Use
this manifest when testing import flows that cross engine/runtime seams:

- On startup, `vulkan_compute_preview.cpp` loads `project_settings.json` via
  `loadModelEntriesFromSettings()`, resolving each listed folder relative to the
  working directory and its parent (`PathTracingRenderer/` and repo-root `..`).
- The import path (`VulkanComputePreview::importModelFromFolder`) takes a folder,
  resolves it, finds the first `scene.gltf`/`.glb` inside, appends a model entry,
  and saves the updated list with `saveModelEntriesToSettings()`, rewriting folder
  paths to canonical form.
- The UI should treat these entries as the authoritative model list; missing
  folders are skipped and logged, duplicates are deduplicated by normalized path.
- A non-selected model keeps preview usable via fallback textures (`selectedModel
  = -1`), and the model-preview shader is enabled only when a model is actively
  loaded and `loadModelSceneResources` has built the GPU buffers.

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
- `vulkan_gltf_flat.comp` is already the transport baseline: it runs full BVH traversal plus
  multi-bounce `rayLogic`-style shading (diffuse/specular/transmission), optional
  shadowing modes, and Russian roulette. Use this as the parity target while extending
  performance and runtime breadth.
- Treat the current `VulkanComputePreview` host-visible pixel-buffer readback as a bridge only. The target renderer should write an accumulation storage image/buffer and eventually avoid CPU readback.

## Hardware Ray Tracing (VK_KHR_ray_tracing_pipeline)

Reference source: upstream `SaschaWillems/Vulkan/examples/raytracingbasic`, `raytracinggltf`, `raytracingcallable`, `raytracingintersection`, `raytracingshadows`, and `raytracingreflections`. These are not vendored locally; use the patterns below and search the upstream repo before writing HW RT code from scratch.

### Required extensions and Vulkan API version

```cpp
apiVersion = VK_API_VERSION_1_1;  // minimum
enabledDeviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
enabledDeviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
// Required by acceleration structure:
enabledDeviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
enabledDeviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
enabledDeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
// Required by ray tracing pipeline:
enabledDeviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
enabledDeviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
```

### Feature chain (getEnabledFeatures / deviceCreatepNextChain)

Chain must be assembled tail-to-head — BufferDeviceAddress is the innermost:

```cpp
enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

deviceCreatepNextChain = &enabledAccelerationStructureFeatures;
enabledFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;  // storage image format decided at runtime
```

For glTF + descriptor indexing also enable:

```cpp
physicalDeviceDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
physicalDeviceDescriptorIndexingFeatures.pNext = &enabledAccelerationStructureFeatures;
deviceCreatepNextChain = &physicalDeviceDescriptorIndexingFeatures;
enabledFeatures.shaderInt64 = VK_TRUE;  // needed for uint64_t buffer device addresses in shaders
```

### Function pointer loading (in prepare())

All RT entry points are KHR extensions; load them from the device:

```cpp
vkGetBufferDeviceAddressKHR                = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>               (vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR"));
vkCmdBuildAccelerationStructuresKHR        = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>       (vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
vkBuildAccelerationStructuresKHR           = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>          (vkGetDeviceProcAddr(device, "vkBuildAccelerationStructuresKHR"));
vkCreateAccelerationStructureKHR           = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>          (vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
vkDestroyAccelerationStructureKHR          = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>         (vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
vkGetAccelerationStructureBuildSizesKHR    = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>   (vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
vkCmdTraceRaysKHR                          = reinterpret_cast<PFN_vkCmdTraceRaysKHR>                         (vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
vkGetRayTracingShaderGroupHandlesKHR       = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>      (vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
vkCreateRayTracingPipelinesKHR             = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>            (vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
```

Also query pipeline properties (needed for SBT sizing):

```cpp
rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
props2.pNext = &rayTracingPipelineProperties;
vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
```

### AccelerationStructure struct

```cpp
struct AccelerationStructure {
    VkAccelerationStructureKHR handle;
    uint64_t deviceAddress = 0;
    VkDeviceMemory memory;
    VkBuffer buffer;
};
```

The `deviceAddress` (from `vkGetAccelerationStructureDeviceAddressKHR`) is what TLAS instances reference, not the `VkBuffer` handle.

### Buffer usage flags for AS inputs

Vertex/index/transform/instance buffers used as AS build inputs need:

```cpp
VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
```

Add `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` when the same buffer is also read by shaders (e.g. vertex data for closest-hit attribute lookups).

The AS storage buffer itself needs:

```cpp
VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
```

All device-address allocations need `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR` in `VkMemoryAllocateFlagsInfo` chained via `pNext`.

### BLAS build sequence (triangle geometry)

```
1. Upload vertex/index/transform buffers (HOST_VISIBLE | HOST_COHERENT for simplicity; stage to DEVICE_LOCAL for perf)
2. Fill VkAccelerationStructureGeometryKHR:
     geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR
     geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT
     geometry.triangles.vertexData.deviceAddress = getBufferDeviceAddress(vertexBuffer)
     geometry.triangles.vertexStride = sizeof(Vertex)
     geometry.triangles.maxVertex = vertexCount - 1
     geometry.triangles.indexType = VK_INDEX_TYPE_UINT32
     geometry.triangles.indexData.deviceAddress = getBufferDeviceAddress(indexBuffer)
     geometry.triangles.transformData = transformBufferDeviceAddress  (identity or per-node)
3. Fill VkAccelerationStructureBuildGeometryInfoKHR:
     type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
     flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
     geometryCount = 1 (or N for multi-geometry BLAS)
4. vkGetAccelerationStructureBuildSizesKHR with primitiveCount[] per geometry → VkAccelerationStructureBuildSizesInfoKHR
5. createAccelerationStructureBuffer (AS storage + device-address memory)
6. vkCreateAccelerationStructureKHR
7. createScratchBuffer(buildSizesInfo.buildScratchSize) — temporary DEVICE_LOCAL, SHADER_DEVICE_ADDRESS
8. Set buildInfo.mode = BUILD, dstAccelerationStructure = handle, scratchData.deviceAddress
9. Fill VkAccelerationStructureBuildRangeInfoKHR: primitiveCount, primitiveOffset=0, firstVertex=0, transformOffset
10. vkCmdBuildAccelerationStructuresKHR in a one-time command buffer → flushCommandBuffer
11. vkGetAccelerationStructureDeviceAddressKHR → store in bottomLevelAS.deviceAddress
12. deleteScratchBuffer
```

For multi-geometry BLAS (one geometry per glTF primitive or per object), pass arrays of `VkAccelerationStructureGeometryKHR` and matching `primitiveCount[]`. Each geometry gets its own `VkAccelerationStructureBuildRangeInfoKHR` with the correct `transformOffset` (stride × geometry index when using a shared transform buffer).

### TLAS build sequence

```
1. Fill VkAccelerationStructureInstanceKHR:
     transform = 3×4 row-major identity (or flip Y for glTF: [1][1] = -1.0f)
     mask = 0xFF
     instanceShaderBindingTableRecordOffset = 0
     flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
     accelerationStructureReference = bottomLevelAS.deviceAddress
2. Upload instances buffer (HOST_VISIBLE | HOST_COHERENT, SHADER_DEVICE_ADDRESS | AS_BUILD_INPUT_READ_ONLY)
3. Fill VkAccelerationStructureGeometryKHR:
     geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR
     geometry.instances.arrayOfPointers = VK_FALSE
     geometry.instances.data.deviceAddress = getBufferDeviceAddress(instancesBuffer)
4. Same size query + create + build pattern as BLAS (type = TOP_LEVEL, primitiveCount = instance count)
5. instancesBuffer.destroy() after flushCommandBuffer (TLAS keeps its own copy)
```

### Storage image for raygen output

```cpp
image.format = swapChain.colorFormat;  // match exactly — no conversion needed in the blit
image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
// After creation: transition UNDEFINED → GENERAL via one-time command buffer
vks::tools::setImageLayout(cmd, storageImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
```

On window resize: destroy image/view/memory, recreate, update the storage image descriptor in all frame descriptor sets.

### Shader Binding Table (SBT) layout and sizing

SBT standard layout:

```
| raygen  |
| miss    |  (can have multiple — e.g. primary + shadow)
| hit     |  (closesthit [+ anyhit [+ intersection]])
| callable |  (optional; one slot per callable shader)
```

Sizing:

```cpp
const uint32_t handleSize        = rayTracingPipelineProperties.shaderGroupHandleSize;
const uint32_t handleSizeAligned = alignedSize(handleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);
// SBT buffer usage:
VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
// Memory: HOST_VISIBLE | HOST_COHERENT (persistent map)
// VkStridedDeviceAddressRegionKHR:
entry.deviceAddress = getBufferDeviceAddress(sbtBuffer);
entry.stride        = handleSizeAligned;
entry.size          = handleSizeAligned;  // (or handleSizeAligned * N for N shaders in this group)
```

After `vkGetRayTracingShaderGroupHandlesKHR` fills `shaderHandleStorage[]`, copy with stride offsets:

```cpp
memcpy(raygen.mapped,   storage.data(),                          handleSize);
memcpy(miss.mapped,     storage.data() + handleSizeAligned,      handleSize);  // * missCount for multiple
memcpy(hit.mapped,      storage.data() + handleSizeAligned * 2,  handleSize);
memcpy(callable.mapped, storage.data() + handleSizeAligned * 3,  handleSize * objectCount);
```

### Shader group types

```cpp
// Ray generation / miss / callable — all use GENERAL:
shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
shaderGroup.generalShader = <stage index>;

// Triangle closest-hit (with optional any-hit):
shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
shaderGroup.closestHitShader = <chit stage>;
shaderGroup.anyHitShader     = <ahit stage>;  // or VK_SHADER_UNUSED_KHR

// Procedural closest-hit + intersection:
shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
shaderGroup.closestHitShader  = <chit stage>;
shaderGroup.intersectionShader = <rint stage>;
```

Always zero-initialize `VkRayTracingShaderGroupCreateInfoKHR` and set unused slots to `VK_SHADER_UNUSED_KHR`.

### Pipeline creation

```cpp
VkRayTracingPipelineCreateInfoKHR ci{};
ci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
ci.stageCount = static_cast<uint32_t>(shaderStages.size());
ci.pStages    = shaderStages.data();
ci.groupCount = static_cast<uint32_t>(shaderGroups.size());
ci.pGroups    = shaderGroups.data();
ci.maxPipelineRayRecursionDepth = std::min(2u, rayTracingPipelineProperties.maxRayRecursionDepth);
ci.layout     = pipelineLayout;
vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
```

### Descriptor layout (canonical bindings)

Basic:
```
Binding 0: VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR  — top-level AS (raygen [+ chit for shadow rays])
Binding 1: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE               — raygen output (raygen only)
Binding 2: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER              — {viewInverse, projInverse [, frame]}
```

Extended for glTF:
```
Binding 3: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER      — unused in basic; texture array in gltf sample
Binding 4: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER              — GeometryNode SSBO (see below)
Binding 5: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER[]    — variable-count texture array (descriptor indexing)
```

Official `raytracingshadows` pattern:
```
Binding 0: VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR  - TLAS, visible to raygen and closest-hit
Binding 1: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE               - raygen output
Binding 2: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER              - {viewInverse, projInverse, lightPos, vertexSize}
Binding 3: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER              - glTF vertex buffer, visible to closest-hit
Binding 4: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER              - glTF index buffer, visible to closest-hit
```

That sample loads a complex glTF scene through `vkglTF::Model`, sets the model
buffer usage flags to:

```cpp
VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
```

It then builds one triangle BLAS from the scene vertex/index buffer device
addresses and places it in one TLAS instance with
`VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR`.

Writing the AS descriptor requires `VkWriteDescriptorSetAccelerationStructureKHR` chained via `pNext` on the `VkWriteDescriptorSet`:

```cpp
VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
asInfo.accelerationStructureCount = 1;
asInfo.pAccelerationStructures    = &topLevelAS.handle;

VkWriteDescriptorSet write{};
write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
write.pNext           = &asInfo;
write.dstBinding      = 0;
write.descriptorCount = 1;
write.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
```

### Command buffer dispatch and blit to swapchain

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1, &descriptorSets[i], 0, 0);
vkCmdTraceRaysKHR(cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt_or_empty, width, height, 1);

// Blit storage image to swapchain (no render pass needed):
setImageLayout(cmd, swapChain.images[idx], UNDEFINED, TRANSFER_DST_OPTIMAL, range);
setImageLayout(cmd, storageImage.image,    GENERAL,   TRANSFER_SRC_OPTIMAL, range);
vkCmdCopyImage(cmd, storageImage.image, TRANSFER_SRC_OPTIMAL, swapChain.images[idx], TRANSFER_DST_OPTIMAL, 1, &region);
setImageLayout(cmd, swapChain.images[idx], TRANSFER_DST_OPTIMAL, PRESENT_SRC_KHR,  range);
setImageLayout(cmd, storageImage.image,    TRANSFER_SRC_OPTIMAL, GENERAL,           range);
```

Pass `&emptySbtEntry` (zero-init `VkStridedDeviceAddressRegionKHR`) for the callable slot when not using callable shaders.

### Frame accumulation pattern

From the `raytracinggltf` example — enables progressive anti-aliasing and stochastic transparency:

```cpp
struct UniformData {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    uint32_t frame{ 0 };
};

// each render():
if (camera.updated) uniformData.frame = -1;  // reset on camera move
uniformData.frame++;
memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(uniformData));
```

The raygen shader uses `frame` as a noise seed to jitter ray directions per sample, accumulating into the storage image across frames. Reset to 0 (via wrapping from -1) on camera or scene change.

**N-Ray mapping:** `params.renderInvalidated` / `params.currentSample` maps directly — pass `currentSample` as the frame counter in the UBO.

### GeometryNode SSBO (glTF multi-primitive)

One entry per glTF primitive, indexed by `gl_GeometryIndexEXT` in the closest-hit shader:

```cpp
struct GeometryNode {
    uint64_t vertexBufferDeviceAddress;  // base of the shared vertex buffer
    uint64_t indexBufferDeviceAddress;   // offset-adjusted pointer to this primitive's first index
    int32_t  textureIndexBaseColor;
    int32_t  textureIndexOcclusion;      // -1 if absent
};
```

Build the BLAS with one `VkAccelerationStructureGeometryKHR` per primitive so `gl_GeometryIndexEXT` in the shader equals the `geometryNodes[]` index. In the closest-hit shader:

```glsl
GeometryNode node = geometryNodes[gl_GeometryIndexEXT];
// reconstruct hit attributes:
uint idx0 = indices[gl_PrimitiveID * 3 + 0] + node.vertexBufferDeviceAddress ...
```

**N-Ray mapping:** `Data::tris[hitIdx].materialIdx` + `Data::materials[]` is the CPU equivalent. The GPU version replaces the fat `Tri` with a compact `GeometryNode` SSBO + the model's vertex/index buffers.

### Descriptor indexing for variable-count texture arrays

When a glTF model has an unknown number of textures at pipeline-creation time:

```cpp
// In descriptor set layout: last binding uses variable count flag
std::vector<VkDescriptorBindingFlagsEXT> bindingFlags = { 0, 0, 0, 0, 0,
    VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT };
VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flagsCI{};
flagsCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
flagsCI.bindingCount = 6;
flagsCI.pBindingFlags = bindingFlags.data();
descriptorSetLayoutCI.pNext = &flagsCI;

// At allocation: tell the pool the actual count
uint32_t varDescCount[] = { imageCount };
VkDescriptorSetVariableDescriptorCountAllocateInfoEXT varAllocInfo{};
varAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
varAllocInfo.descriptorSetCount = 1;
varAllocInfo.pDescriptorCounts  = varDescCount;
allocInfo.pNext = &varAllocInfo;
```

Shader side requires `#extension GL_EXT_nonuniform_qualifier : enable` and `nonuniformEXT()` when indexing with a non-uniform value like `gl_GeometryIndexEXT`.

### Callable shaders

Add per-geometry callable shader groups after raygen/miss/hit in the SBT:

```cpp
// For objectCount callables:
for (uint32_t i = 0; i < objectCount; i++) {
    shaderStages.push_back(loadShader("callable" + to_string(i+1) + ".rcall.spv", VK_SHADER_STAGE_CALLABLE_BIT_KHR));
    shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroup.generalShader = shaderStages.size() - 1;
    shaderGroups.push_back(shaderGroup);
}
// Callable SBT holds objectCount handles:
createShaderBindingTable(callable, objectCount);
memcpy(callable.mapped, storage.data() + handleSizeAligned * 3, handleSize * objectCount);
```

In the closest-hit shader: `executeCallableEXT(gl_GeometryIndexEXT, payloadLocation)` — dispatches to the callable at SBT slot `gl_GeometryIndexEXT`. The callable SBT entry (4th argument to `vkCmdTraceRaysKHR`) must be non-empty.

### Intersection shaders (procedural geometry)

Replace triangle BLAS geometry with AABB geometry:

```cpp
// BLAS geometry:
accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
accelerationStructureGeometry.geometry.aabbs.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
accelerationStructureGeometry.geometry.aabbs.data.deviceAddress = getBufferDeviceAddress(aabbsBuffer);
accelerationStructureGeometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);  // 6 floats

// Hit group type is PROCEDURAL (not TRIANGLES):
shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
shaderGroup.closestHitShader   = <chit stage>;
shaderGroup.intersectionShader = <rint stage>;
```

The intersection shader (`.rint`) runs once per AABB hit candidate. It tests the analytical shape (e.g. sphere) and calls `reportIntersectionEXT(t, hitKind)` if the ray actually hits it. The closest-hit shader is only invoked after a successful `reportIntersectionEXT`. Sphere data is passed as a separate SSBO indexed by `gl_PrimitiveID`.

### Recursive secondary rays — shadows and reflections

References: upstream `SaschaWillems/Vulkan/examples/raytracingshadows` and `raytracingreflections`. Both extend `raytracingbasic` by launching a **second ray after the first hit**. Shadows trace toward the light; reflections trace along the reflected direction. The two examples demonstrate the two ways to structure secondary bounces, and the choice matters for N-Ray.

#### Two structuring patterns (pick deliberately)

| Pattern | Where the secondary ray is launched | Recursion depth needed | Maps to |
|---|---|---|---|
| **Recursion from closest-hit** (shadows) | `traceRayEXT` inside `.rchit` | grows with bounce count — `maxPipelineRayRecursionDepth ≥ bounces+1` | a single occlusion/shadow probe per hit |
| **Iterate in raygen** (reflections) | closest-hit only *returns* surface data via payload; raygen reflects and re-traces in a `for` loop | stays at **1** regardless of bounce count | N-Ray's `rayLogic` bounce loop — **prefer this** |

Hardware recursion is capped (`maxPipelineRayRecursionDepth`, often 1–2 on real GPUs and always `≥ rayTracingPipelineProperties.maxRayRecursionDepth`). Deep path-tracer bounce counts will exceed it. The reflections example sidesteps this: the closest-hit shader writes `{color, distance, normal, reflector}` into the payload and returns; the raygen shader inspects the payload, computes the reflected origin/direction, attenuates throughput, and calls `traceRayEXT` again in a bounded loop. This is exactly how N-Ray's `rayLogic` already works on the CPU, so the iterate-in-raygen form is the natural port — keep `maxPipelineRayRecursionDepth = 1` and drive bounces from the loop. Reserve closest-hit recursion for a single short shadow probe (depth 2) if you add direct lighting.

#### Payload design

Closest-hit communicates with the launching shader only through the ray payload (`rayPayloadEXT` / `rayPayloadInEXT` at a matching `location`). A reflection/path-tracer payload carries enough to continue the loop in raygen:

```glsl
struct RayPayload {
    vec3  color;       // surface contribution at this hit
    float distance;    // gl_RayTmaxEXT — used to compute the hit point
    vec3  normal;      // world-space shading normal for the reflected direction
    float reflector;   // 0 = stop, >0 = spawn a reflection ray (or a material/throughput field)
};
layout(location = 0) rayPayloadEXT RayPayload payload;   // raygen
// closest-hit: layout(location = 0) rayPayloadInEXT RayPayload payload;
```

Shadow rays use a **separate, tiny payload at a different location** (e.g. `layout(location = 2) rayPayloadEXT bool shadowed;`) so the occlusion result does not clobber the primary payload. The shadow miss shader sets `shadowed = false`; default it to `true` before the trace and use `gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT` so any hit between surface and light proves occlusion without invoking a hit shader.

#### Multiple miss shaders and SBT layout

Shadow rays need a *second* miss shader. The miss group then holds two handles and `vkCmdTraceRaysKHR`'s miss `sbtRecordOffset`/index selects which one. From the shadows sample:

```cpp
createShaderBindingTable(shaderBindingTables.raygen, 1);
createShaderBindingTable(shaderBindingTables.miss,   2);   // primary miss + shadow miss
createShaderBindingTable(shaderBindingTables.hit,    1);

const uint32_t handleSizeAligned = alignedSize(handleSize, shaderGroupHandleAlignment);
memcpy(raygen.mapped, storage.data(),                         handleSize);
memcpy(miss.mapped,   storage.data() + handleSizeAligned,     handleSize * 2);  // two contiguous handles
memcpy(hit.mapped,    storage.data() + handleSizeAligned * 3, handleSize);      // raygen=0, miss=1, shadowmiss=2, hit=3
```

Both miss shaders are pushed as `VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR` groups, back to back, before the hit group. In GLSL, the shadow trace selects the second miss shader via its miss index argument to `traceRayEXT` (`missIndex = 1`).

#### Descriptor differences from raytracingbasic

The secondary ray needs scene attributes and an AS reachable from the hit shader:

```cpp
// Binding 0: TLAS must now be visible to CLOSEST_HIT too (it launches traceRayEXT):
descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0);
// Binding 2: UBO with light position visible to RAYGEN | CLOSEST_HIT | MISS:
descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 2);
// Bindings 3,4: vertex + index buffers as STORAGE_BUFFER for CLOSEST_HIT attribute reconstruction:
descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 3);
descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 4);
```

The vertex/index buffers must be created with `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` *in addition to* the AS-build-input flags (the shadows sample sets all three usage bits on the glTF loader's buffers — see "Buffer usage flags for AS inputs"). Size the descriptor pool for the extra storage buffers (`STORAGE_BUFFER` count = `maxConcurrentFrames * 2`). The closest-hit shader reconstructs the interpolated normal from the three vertices of `gl_PrimitiveID` (using barycentrics from `hitAttributeEXT`) and `uniformData.vertexSize` for manual struct unpacking.

#### Recursion depth and per-bounce origin offset

```cpp
ci.maxPipelineRayRecursionDepth = std::min(2u, rayTracingPipelineProperties.maxRayRecursionDepth);
```

Set this to the actual recursion you use (2 for primary + shadow), not the bounce count when iterating in raygen. Always re-launch secondary rays with the origin nudged off the surface (`hitPoint + normal * tmin`, `tmin ≈ 0.001`) to avoid self-intersection acne — the GPU analogue of the CPU path tracer's epsilon offset after each bounce.

**N-Ray mapping:** the `rayLogic` bounce loop becomes the raygen loop; per-bounce material branching (diffuse/specular/refraction) decides the next direction and throughput written back through the payload; `reflector`/material id in the payload replaces the CPU material lookup. Russian roulette termination ports verbatim as the loop's early-exit. Add a single shadow trace (second miss shader, depth-2 recursion) only if/when you add next-event-estimation direct lighting.

### N-Ray mapping for hardware ray tracing

| N-Ray CPU concept | Vulkan HW RT equivalent |
|---|---|
| `globalCompactBVH` (flattened SAH BVH) | Replaced entirely by hardware BLAS/TLAS — driver manages the tree |
| `Data::tris` fat triangle array | Vertex/index buffers for BLAS inputs + `GeometryNode` SSBO for shading attribute lookups |
| `Data::triIsect` compact traversal mirror | Eliminated — hardware traversal does not read this |
| `Data::materials` | Material SSBO indexed via `GeometryNode.textureIndexBaseColor` or a material index per geometry |
| `traverseFlatBVH` + `rayAABB` + `RayIntersectsTriangle` | Handled by hardware; raygen calls `traceRayEXT()`, driver does the traversal |
| `rayLogic` bounce loop | **Iterate-in-raygen** loop (`raytracingreflections` pattern): closest-hit returns surface data via the payload, raygen reflects/re-traces — keeps `maxPipelineRayRecursionDepth = 1`. Avoid deep closest-hit recursion |
| `specularLighting` reflected direction | Reflected ray spawned in the raygen loop from the payload `normal`, origin offset by `normal * tmin` |
| Shadow/occlusion probe (future NEE direct lighting) | Second miss shader + `traceRayEXT` from closest-hit with `TerminateOnFirstHit | SkipClosestHit` (`raytracingshadows` pattern), recursion depth 2, separate shadow payload location |
| `accumBuffer` (float HDR accumulation) | Storage image accumulation via `frame` counter in UBO |
| `params.renderInvalidated` (restart worker) | `uniformData.frame = -1` to reset accumulation |
| `params.russianRoulette` | Same unbiased termination logic, fully portable to GLSL |
| `ObjImporter` (one model → one BLAS geometry) | One BLAS geometry per glTF primitive; multi-model scenes use a multi-geometry BLAS or multiple BLAS with TLAS instances |

The initial HW RT port does not need to replicate the full PBR stack immediately — a raygen + miss (sky color) + closesthit (normal visualization or flat albedo) produces a working image first. Port `rayAABB` / triangle intersection logic only if debugging the hardware AS build; for production use, trust the hardware.

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
