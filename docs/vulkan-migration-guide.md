# Vulkan Migration Guide: Transitioning to GPU Path Tracing

This document outlines the strategic roadmap for migrating the **N-Ray** project from its current CPU-based path-tracing implementation to a fully GPU-accelerated renderer using **Vulkan**.

For the concrete compute-shader path tracing buffer contract and staged migration
plan, see [`docs/vulkan-compute-path-tracing-plan.md`](vulkan-compute-path-tracing-plan.md).
For the Vulkan memory allocation policy, see
[`docs/vulkan-memory-allocator-integration.md`](vulkan-memory-allocator-integration.md).
For glTF asset and PBR material import policy, see
[`docs/gltf-vulkan-pbr-import.md`](gltf-vulkan-pbr-import.md).

## Architectural Overview

The current architecture relies on `omp.h` (OpenMP) for CPU parallelism and custom OBJ parsing. Migrating to Vulkan requires shifting from software-managed loops to hardware-accelerated pipelines and explicit GPU memory management.

The current migration direction is Vulkan **compute shaders** (`VK_PIPELINE_BIND_POINT_COMPUTE`) rather than `VK_KHR_ray_tracing_pipeline`. Compute shaders do not consume automatic vertex inputs or hardware triangle state; the CPU must flatten scene geometry, materials, and BVH nodes into descriptor-bound storage buffers.

Future hardware ray tracing work should use the official Sascha Willems
`raytracingshadows` example as the direct-light shadow reference, alongside
`raytracingbasic` for bring-up and `raytracinggltf` for glTF geometry/material
descriptors. The shadow sample's useful contract is: glTF vertex/index buffers
created with acceleration-structure input, shader-device-address, and
storage-buffer usage; one triangle BLAS referenced by one TLAS instance;
descriptors for TLAS, storage image, uniform data, vertex SSBO, and index SSBO;
raygen plus primary miss plus shadow miss plus closest-hit shader groups; a miss
SBT with two records; and recursion depth clamped to two so closest-hit can trace
shadow rays.

PBR material work is not the same thing as ray tracing work. PBR is the
micro-scale BRDF evaluated at one shaded point, and it can be used by a normal
Vulkan graphics/raster pipeline just as well as by a path tracer. The Vulkan
compute migration is about the macro-scale transport loop: generating rays,
traversing BVHs, choosing bounces, accumulating samples, and displaying the
result. N-Ray still needs glTF PBR import because those material parameters are
what the path tracer will evaluate after each hit. See
[`docs/gltf-vulkan-pbr-import.md`](gltf-vulkan-pbr-import.md#pbr-is-the-material-model-not-the-transport-algorithm)
for the detailed explanation and comparison.

---

## 1. Asset & Scene Loading (The PBR Frontend)

Path-tracers require high-fidelity asset loading (especially material metadata like roughness, metallic, albedo maps, and normal maps).

### Rework Areas:
*   **Flatten renderer-owned scene data first:** Today, `ObjImporter` already converts OBJ files into `Data::tris`, `Data::triIsect`, `Data::materials`, `Data::models`, and `globalCompactBVH`. The first Vulkan path should upload those existing arrays to storage buffers before replacing the asset loader.
*   **Use the local glTF validation corpus:** Top-level `assets/` contains the project-owned validation models for importer work. Use `assets/*/scene.gltf` or `.glb` bundles, with their folder-local buffers and textures, when validating glTF parsing, PBR material conversion, texture color-space policy, tangent generation, and Vulkan upload layouts. These assets are separate from the current `PathTracingRenderer/models/` OBJ runtime scene.
*   **Replace `ObjImporter` later:** Currently, `PathTracingRenderer/include/objImporter.h` uses a manual `ifstream` parser. Once the GPU buffer contract is stable, this should be replaced with:
    *   **[tinygltf](https://github.com/syoyo/tinygltf):** A lightweight glTF 2.0 parser. Prefer the stable v2 C++ header for production importer work until the experimental v3 C runtime settles, and keep the parser isolated from N-Ray's renderer-owned GPU upload structs. For a proven tinygltf→engine conversion pattern (accessor decode, node/TRS traversal, index types, tangents, per-material handling), read the vendored reference loaders under [`tutorials/saschawillems/gltf/`](../tutorials/saschawillems/gltf/) and the rules in [`docs/gltf-vulkan-pbr-import.md`](gltf-vulkan-pbr-import.md) before designing the importer.
    *   **[assimp](https://github.com/assimp/assimp):** If support for legacy formats (FBX, OBJ) is still required, Assimp can process complex hierarchical scene graphs into clean vertex and index arrays.
*   **Texture Management:**
    *   **[stb_image](https://github.com/nothings/stb):** Enhance usage of `stb_image` for loading multi-channel image layouts (PNG, JPG) and high-dynamic-range environment maps (HDR) for Image-Based Lighting (IBL) calculations. Preserve glTF color-space policy: base color and emissive are sRGB, while metallic-roughness, occlusion, and normal textures are linear data.

---

## 2. Vulkan Memory & Pipeline Scaffolding

Vulkan is extremely verbose regarding resource synchronization and memory allocations. These libraries eliminate boilerplate without abstracting away low-level API control:

### Key Libraries:
*   **[Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator):** Recommended once the Vulkan path moves beyond the current single-buffer compute preview. VMA should own sub-allocations for scene SSBOs, staging buffers, accumulation images/buffers, texture images, and any future hardware ray-tracing backing buffers, while a small N-Ray resource layer keeps Vulkan handles available for descriptors and command recording.
*   **[volk](https://github.com/zeux/volk):** A dynamic function loader for Vulkan. It eliminates repeated driver redirection overhead by initializing all device-level function pointers directly from your physical hardware handle.

---

## 3. Shader Infrastructure & Compilation

Since the goal is migrating the CPU logic (found in `renderer.cpp` and `render_worker.cpp`) to the GPU, we need robust bytecode tools.

### Strategies:
*   **Target:** **Compute Shaders** for the current migration. Hardware ray tracing extensions can be revisited later, but the immediate renderer must manually traverse flattened triangle and BVH storage buffers.
*   **Compilers:**
    *   **[DirectXShaderCompiler (DXC)](https://github.com/microsoft/DirectXShaderCompiler):** Recommended for writing shaders in modern HLSL rather than GLSL. DXC provides robust SPIR-V code generation.
    *   **[Slang](https://github.com/shader-slang/slang):** An advanced alternative shading language that simplifies modularity in large raytracing pipelines.
*   **Reflection:**
    *   **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross):** Performs reflection on SPIR-V files. This allows the C++ runtime to automatically discover uniform bindings and descriptor set layouts rather than hardcoding resource offsets.

---

## 4. Mathematical Infrastructure

*   **[GLM (OpenGL Mathematics)](https://github.com/g-truc/glm):** Continue using GLM. It is already integrated in the project (`PathTracingRenderer/external/glm/`) and is perfectly designed for data alignment between CPU hosting pools and GPU shader uniforms.

---

## 5. Diagnostics, Validation & Testing

Vulkan drivers do not perform hand-holding. To safely validate render loops, utilize these platforms:

### Tools:
*   **Vulkan Validation Layers:** Bundled inside the official LunarG Vulkan SDK. Essential for tracking buffer allocation hazards and memory state synchronization issues in real-time.
*   **[RenderDoc](https://renderdoc.org/):** The preferred manual frame debugger for the Vulkan compute preview. Use it to inspect compute dispatches, descriptor sets, storage buffers, storage images, glTF textures, denoiser resources, and shadow/refraction state when terminal logs or screenshots are not enough. RenderDoc does not introspect Vulkan/D3D12 ray-tracing pipeline work; future HWRT captures can replay ray-tracing results for later passes, but the ray-tracing work itself is opaque. See [`renderdoc-vulkan-debugging.md`](renderdoc-vulkan-debugging.md) for the N-Ray launch/capture workflow.
*   **[apitrace](https://apitrace.github.io/):** Useful for tracing and intercepting API graphics streams to filter out complex multi-threaded concurrency validation bugs.

---

## Next Steps for Development

1.  **Initialize Vulkan Instance:** Integrate `volk` and set up the basic Vulkan boilerplate.
2.  **Integrate VMA:** Keep the current preview allocation as a bridge, then introduce a VMA-backed Vulkan resource layer before adding scene SSBOs or accumulation images.
3.  **Transition Scene Data:** Convert `Data::triIsect`, triangle shading data, `PBRMaterial`, and `globalCompactBVH` into GPU upload structs and Vulkan storage buffers.
4.  **Implement Compute Shader Path:** Port the primary-ray closest-hit path first (`rayAABB`, `RayIntersectsTriangle`, `traverseFlatBVH`), then add material evaluation, bounces, and progressive accumulation.
5.  **Add glTF Import:** After the GPU buffer contract is stable, flatten glTF meshes/materials into N-Ray's native traversal, shading, material, texture, and BVH upload data. Use the vendored [`tutorials/saschawillems/gltf/`](../tutorials/saschawillems/gltf/) loaders as the canonical conversion reference (import-frontend only — flatten into native upload structs, do not bind their graphics vertex buffers to compute).
