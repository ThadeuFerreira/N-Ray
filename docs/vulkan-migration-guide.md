# Vulkan Migration Guide: Transitioning to GPU Path Tracing

This document outlines the strategic roadmap for migrating the **N-Ray** project from its current CPU-based path-tracing implementation to a fully GPU-accelerated renderer using **Vulkan**.

## Architectural Overview

The current architecture relies on `omp.h` (OpenMP) for CPU parallelism and custom OBJ parsing. Migrating to Vulkan requires shifting from software-managed loops to hardware-accelerated pipelines (Compute or Ray Tracing) and explicit GPU memory management.

---

## 1. Asset & Scene Loading (The PBR Frontend)

Path-tracers require high-fidelity asset loading (especially material metadata like roughness, metallic, albedo maps, and normal maps).

### Rework Areas:
*   **Replace `ObjImporter`:** Currently, `PathTracingRenderer/include/objImporter.h` uses a manual `ifstream` parser. This should be replaced with:
    *   **[tinygltf](https://github.com/syoyo/tinygltf):** A header-only C++ library for loading glTF 2.0 files. glTF is the gold standard for PBR because its material definitions natively conform to standard PBR metallic-roughness models.
    *   **[assimp](https://github.com/assimp/assimp):** If support for legacy formats (FBX, OBJ) is still required, Assimp can process complex hierarchical scene graphs into clean vertex and index arrays.
*   **Texture Management:**
    *   **[stb_image](https://github.com/nothings/stb):** Enhance usage of `stb_image` for loading multi-channel image layouts (PNG, JPG) and high-dynamic-range environment maps (HDR) for Image-Based Lighting (IBL) calculations.

---

## 2. Vulkan Memory & Pipeline Scaffolding

Vulkan is extremely verbose regarding resource synchronization and memory allocations. These libraries eliminate boilerplate without abstracting away low-level API control:

### Key Libraries:
*   **[Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-Libraries/VulkanMemoryAllocator):** Critical for handling sub-allocations of raw GPU `VkDeviceMemory`. Instead of manually tracking bindings in `renderer.cpp`, VMA provides simple allocation functions while optimizing memory page layouts.
*   **[volk](https://github.com/zeux/volk):** A dynamic function loader for Vulkan. It eliminates repeated driver redirection overhead by initializing all device-level function pointers directly from your physical hardware handle.

---

## 3. Shader Infrastructure & Compilation

Since the goal is migrating the CPU logic (found in `renderer.cpp` and `render_worker.cpp`) to the GPU, we need robust bytecode tools.

### Strategies:
*   **Target:** Likely **Compute Shaders** (for compatibility) or **Vulkan Ray Tracing extensions** (`VK_KHR_ray_tracing_pipeline`).
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
*   **[RenderDoc](https://renderdoc.org/):** The industry standard for deep frame debugging. Allows inspection of memory footprints, bounding hierarchy nodes (BVH), and stepping through active shaders.
*   **[apitrace](https://apitrace.github.io/):** Useful for tracing and intercepting API graphics streams to filter out complex multi-threaded concurrency validation bugs.

---

## Next Steps for Development

1.  **Initialize Vulkan Instance:** Integrate `volk` and set up the basic Vulkan boilerplate.
2.  **Integrate VMA:** Replace manual buffer management with VMA-backed allocations.
3.  **Implement Compute Shader Path:** Port the logic in `PathTracer::RayIntersectsTriangle` and `PathTracer::CalculatePath` to HLSL/GLSL compute shaders.
4.  **Transition Scene Data:** Update `Scene` and `Data` structures to be compatible with GPU Buffer/Texture descriptors.
