# Vulkan Memory Allocator Integration Notes

This document captures the intended use case for
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
(VMA) in N-Ray's Vulkan migration. The short version: VMA should be the default
allocator for the real Vulkan renderer, but it is not urgent for the current
single-buffer compute preview.

## What VMA Provides

VMA is an MIT-licensed, single-header Vulkan memory allocation library published
by AMD GPUOpen. It works on any Vulkan-capable GPU, not only AMD hardware. The
public API follows Vulkan's C style while the implementation is C++.

Vulkan separates resource objects from backing memory: creating a `VkBuffer` or
`VkImage` is not enough, because the app must also query memory requirements,
choose a compatible memory type, allocate `VkDeviceMemory`, and bind the
resource to that memory. Vulkan also exposes a device limit named
`maxMemoryAllocationCount`; Khronos recommends large allocations with
application-side suballocation instead of one driver allocation per resource.

VMA handles that middle layer:

- Chooses compatible memory types from intended usage and access flags.
- Allocates larger `VkDeviceMemory` blocks and suballocates ranges for buffers
  and images.
- Creates and binds buffers/images in one call through helpers such as
  `vmaCreateBuffer` and `vmaCreateImage`.
- Handles mapped allocations, non-coherent flush/invalidate alignment, and
  persistent mapping patterns.
- Exposes memory budget and statistics APIs, including support for
  `VK_EXT_memory_budget` when the app enables it.
- Supports custom pools, linear allocator pools, allocation names/user data,
  JSON memory dumps, resource aliasing, and defragmentation workflows.

References:

- [AMD GPUOpen VMA overview](https://gpuopen.com/vulkan-memory-allocator/)
- [VMA GitHub README](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [VMA releases](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases)
- [Khronos Vulkan memory allocation guide](https://docs.vulkan.org/guide/latest/memory_allocation.html)

## Fit For N-Ray

The current CPU renderer already has renderer-owned scene data that maps cleanly
to Vulkan storage buffers:

- `Data::triIsect` for compact intersection data.
- Triangle shading data derived from `Data::tris`.
- `Data::materials`.
- `globalCompactBVH`.

The first real GPU path will need several resource classes, not just the toy
pixel buffer used by `VulkanComputePreview`:

- Mostly-static scene SSBOs for triangles, materials, and BVH nodes.
- Upload/staging buffers for scene data and later texture data.
- Per-frame uniform or storage buffers for camera, sample index, and render
  settings.
- Accumulation storage images or buffers.
- Readback/debug buffers during bring-up.
- Environment maps and future albedo/normal/roughness/metallic textures.
- Future hardware ray tracing buffers, if the project later moves from compute
  traversal to `VK_KHR_ray_tracing_pipeline`.

Using raw `VkDeviceMemory` directly for all of those resources would spread
memory-type selection, allocation sizing, binding, mapping, flush/invalidate,
budget checks, and cleanup rules through the renderer. VMA keeps that ownership
inside a small Vulkan resource layer while still exposing Vulkan handles for
descriptor updates and command recording.

One terminology correction: samplers do not own GPU memory. Buffers, images, and
acceleration-structure backing or scratch buffers do. Descriptor arrays and
sampler objects still have lifetime and descriptor-pool costs, but they are not
the reason to use a device-memory allocator.

## Current Repository State

N-Ray already vendors VMA under `vendor/VulkanMemoryAllocator`, and
`README.md` lists it as a staged Vulkan-port dependency. The local vendored
checkout observed during this investigation reports:

```text
v3.3.0-68-ge386068
```

GitHub showed `v3.4.0` as the latest VMA release on 2026-06-04. Do not update
the vendored dependency as part of documentation-only or renderer-behavior work.
When implementation starts, first decide whether to keep the current vendored
revision or update it intentionally in a dedicated dependency change.

Also check dependency hygiene before staging: `vendor/VulkanMemoryAllocator` is
currently a nested Git checkout. If the project wants this dependency tracked as
a submodule, commit the submodule metadata deliberately. If it wants vendored
source without submodule semantics, avoid accidentally committing a gitlink.

## Recommended Integration Policy

New N-Ray Vulkan resource code should allocate through VMA by default. Raw
`vkAllocateMemory`, `vkFreeMemory`, `vkBindBufferMemory`, and
`vkBindImageMemory` should be limited to tutorial/reference code or a documented
special case.

The intended shape is:

- Own one `VmaAllocator` per Vulkan device/context and destroy it after all
  VMA-backed resources are released.
- Store buffers as `VkBuffer + VmaAllocation` and images as
  `VkImage + VmaAllocation`.
- Use `VMA_MEMORY_USAGE_AUTO` for first-pass integration, then tighten choices
  only when profiling or validation shows a need.
- Use host-access flags for mapped allocations:
  `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for staging uploads
  and `VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT` for readback/debug buffers.
- Prefer staged, device-local storage buffers/images for the real path tracer.
  Host-visible buffers are acceptable for early correctness bring-up, not for
  the final renderer hot path.
- Enable `VK_EXT_memory_budget` when available and pass
  `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT` to the allocator.
- If buffer device address becomes part of the GPU data model, enable the Vulkan
  feature/extension first and create VMA with
  `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`.
- Name allocations during development so VMA dumps and validation/debug tooling
  can identify scene buffers, accumulation targets, staging buffers, and texture
  resources clearly.

## Integration Timing

Do not block the current `VulkanComputePreview` on VMA. It allocates one
host-visible storage buffer and reads it back into the existing raylib texture,
so the current manual allocation code is tolerable as a bridge.

Introduce VMA when the Vulkan renderer grows past that bridge:

1. Add a Vulkan resource layer that owns the `VmaAllocator`.
2. Convert the preview pixel buffer or first scene SSBO to a VMA-backed buffer
   as the smallest smoke test.
3. Add VMA-backed scene buffers for `triIsect`, triangle shading data,
   materials, and `globalCompactBVH`.
4. Move mostly-static scene data to staged device-local buffers after the
   closest-hit debug shader is correct.
5. Allocate accumulation output as a VMA-backed storage image or storage buffer.
6. Add texture resources and custom pools only after the core path tracing data
   flow is stable.

## Non-goals

- VMA does not replace descriptor set allocation, synchronization, queue
  ownership, image layouts, shader layout design, or render graph decisions.
- VMA does not automatically move Vulkan resources during defragmentation. It
  can propose allocation moves, but the renderer must recreate/copy/rebind
  resources as required by Vulkan.
- VMA is not a high-level rendering abstraction. It should sit below N-Ray's
  Vulkan resource wrappers, not leak through scene import, UI, or path-tracing
  shader contracts.
