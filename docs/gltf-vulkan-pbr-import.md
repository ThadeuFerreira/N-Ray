# glTF PBR Import For The Vulkan Path

This document describes how N-Ray should import glTF 2.0 assets into the future
Vulkan path tracer while preserving physically based material intent. It is a
documentation-only planning note; the current renderer still uses `ObjImporter`
and CPU-owned arrays.

References:

- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Khronos glTF PBR overview](https://www.khronos.org/gltf/pbr)
- [TinyGLTF](https://github.com/syoyo/tinygltf)
- [Vulkan descriptor set specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)
- [Vulkan memory allocation guide](https://docs.vulkan.org/guide/latest/memory_allocation.html)

## Role In N-Ray

glTF should become the preferred asset interchange format once the Vulkan GPU
buffer contract is stable. It should not be the first Vulkan milestone. The
first compute path should still upload the renderer-owned data that already
exists today: `data.triIsect`, triangle shading data derived from `data.tris`,
`data.materials`, and `globalCompactBVH`.

After that contract is proven, a glTF importer should replace the hardcoded
`ObjImporter` scene frontend by flattening glTF meshes and materials into the
same GPU upload model. If raylib `Model` or `Mesh` loading is used during
transition, treat it as an import frontend only: copy CPU mesh data into N-Ray's
native Vulkan staging structs and do not bind raylib/OpenGL mesh buffers to the
compute renderer.

## Local Validation Corpus

The top-level `assets/` directory is N-Ray's local glTF validation corpus. Use
these bundles as the first importer/PBR validation source before reaching for
external sample repositories:

- `assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf`
- `assets/accurate_torvosaurus_tanneri/scene.gltf`
- `assets/beretta_arx160/scene.gltf`
- `assets/beretta_m9_gameready/scene.gltf`

Each validation bundle should keep its `scene.gltf` or `.glb`, binary buffers,
textures, and license/source files together in the same subfolder. Folders under
`assets/` that do not contain a glTF/GLB scene file are source material only
until an importable scene entry point is added.

These assets are separate from the current `PathTracingRenderer/models/` and
`PathTracingRenderer/textures/` runtime assets used by the OBJ-based CPU path.
They should drive validation for glTF accessor decoding, PBR material mapping,
texture color-space handling, tangent generation, and Vulkan upload layout.

## Geometry Contract

For a compute path tracer, shader access is global and data-oriented. Rays do not
receive draw-call state, so all geometry needed for traversal and shading must be
available through descriptor-bound buffers.

N-Ray should keep the CPU optimization already used by the renderer:

- Compact traversal data is separate from larger shading data.
- Material data is indexed only after the closest hit is known.
- BVH nodes are flattened and traversed explicitly in the shader.

A future glTF import path can still maintain conventional global vertex and
index arrays as staging/import records, but the path-tracing shader should
consume traversal-oriented buffers:

- `GpuTriIntersect`: positions or precomputed edges, triangle id, sidedness.
- `GpuTriShading`: normals, tangents, UVs, material id, object/primitive id.
- `GpuMaterial`: scalar material factors and texture descriptor indices.
- `GpuBvhNode`: flattened BVH node bounds and child/leaf metadata.
- Optional `GpuMeshInstance` or primitive table: ranges, material defaults, and
  object transforms for build/debug tools.

If a hardware ray tracing backend is added later, Vulkan/GLSL built-ins such as
`InstanceID` and `PrimitiveID` become relevant. In the current compute-first
plan, the shader owns traversal, so the hit record should carry the primitive,
material, and instance indices explicitly.

Use explicit `vec4`/`uvec4`-style layouts for GPU structs. Do not copy C++ glTF
loader structs directly into SSBOs. `std430` alignment, `glm::vec3` padding,
normalized integer accessors, and optional attributes must be resolved into
documented upload structs with static size/offset assertions.

## Material And Texture Contract

Path tracing cannot switch texture bindings per bounce. A ray may hit many
materials in one dispatch, so material lookups must be data-driven:

- Store material scalar factors and texture indices in a material SSBO.
- Store all scene textures in descriptor arrays, usually sampled image or
  combined image sampler arrays.
- Use sentinel texture indices for missing maps, or bind default 1x1 fallback
  textures for white, black, flat normal, and default metallic-roughness.

In Vulkan, "bindless textures" normally means a large descriptor array indexed
from shader data, not a `sampler2DArray` image. Texture arrays require matching
dimensions and formats; glTF assets commonly have independent image sizes and
formats. Descriptor indexing is the better fit for an asset library.

The descriptor-indexing path should require, at minimum:

- `runtimeDescriptorArray`.
- `shaderSampledImageArrayNonUniformIndexing`.
- `descriptorBindingVariableDescriptorCount`.
- `descriptorBindingPartiallyBound`, if sparse/default-filled descriptor arrays
  are used during asset streaming.

When texture indices diverge across lanes, use the shader-side non-uniform
indexing qualifier expected by the chosen shader language/compiler. Keep the
material SSBO layout independent from descriptor binding order so the renderer
can migrate from descriptor indexing to descriptor buffers later if desired.

## glTF PBR Rules

glTF 2.0's default material model is metallic-roughness PBR. To preserve authored
material intent, importers must follow the glTF channel and color-space rules
instead of guessing from file names.

Base color:

- `baseColorFactor` defaults to `[1, 1, 1, 1]`.
- `baseColorTexture` RGB is sRGB and must be decoded to linear before lighting.
- Alpha is linear coverage and is controlled by `alphaMode` and `alphaCutoff`.
- Vertex `COLOR_0`, when present, multiplies base color in linear space.

Metallic-roughness:

- `metallicFactor` defaults to `1`.
- `roughnessFactor` defaults to `1`.
- `metallicRoughnessTexture` is one packed texture.
- Roughness is sampled from the G channel.
- Metallic is sampled from the B channel.
- R and A are ignored for metallic-roughness calculations.
- The texture uses a linear transfer function, not sRGB.

Occlusion:

- `occlusionTexture` samples occlusion from the R channel.
- Other channels are ignored for occlusion.
- `strength`, when present, scales the effect as defined by the glTF spec.
- A common art pipeline stores occlusion in the R channel of the same image file
  used for metallic-roughness, but glTF still references occlusion through its
  own material texture slot. Preserve the texture reference and channel rule.

Normal:

- `normalTexture` is tangent-space data in linear space.
- glTF normal maps use +X right, +Y up, +Z toward the viewer.
- Map sampled RGB with `sample * 2 - 1`, apply `normalTexture.scale` to XY when
  present, then normalize.
- Do not apply sRGB decoding to normal maps.

Emissive:

- `emissiveTexture` RGB is sRGB and must be decoded to linear before lighting.
- `emissiveFactor` defaults to `[0, 0, 0]`.
- The exact exposure mapping is renderer policy unless a glTF extension defines
  physical emissive units.

Recommended Vulkan image formats:

- Base color and emissive: `VK_FORMAT_R8G8B8A8_SRGB` when uploaded as 8-bit RGBA
  textures.
- Metallic-roughness, occlusion, and normal: `VK_FORMAT_R8G8B8A8_UNORM` for
  8-bit RGBA storage without transfer-function decode.
- Preserve higher precision source formats when required by asset quality, but
  keep the same sRGB-versus-linear policy.

## Tangents And TBN

Normal mapping requires a tangent-space basis at the hit point. The shader needs
T, B, and N after interpolation, transform, and normalization.

glTF meshes may provide `TANGENT` as `vec4`; XYZ is the tangent direction and W
is the bitangent handedness. When tangents exist:

- Interpolate normal and tangent.
- Re-orthogonalize tangent against normal.
- Compute bitangent as `cross(normal, tangent.xyz) * tangent.w`.

When tangents are missing and a normal texture is used, the importer should
generate tangents using MikkTSpace-compatible logic from positions, normals, and
the selected texture coordinates. The glTF specification recommends MikkTSpace
for this case. Generated tangents should be stored in the GPU shading buffer so
the path tracer does not do per-hit tangent reconstruction work.

Do not add a global "invert normal Y" default for glTF. glTF's normal convention
is OpenGL-style +Y. If N-Ray later imports non-glTF source data or artist-authored
DirectX normal bakes, carry an explicit material/import flag and flip the green
channel only for those assets.

## TinyGLTF Choice

TinyGLTF is the likely parser for N-Ray because it is lightweight, MIT-licensed,
and directly targets glTF 2.0.

As of 2026-06-05, TinyGLTF upstream presents two viable tracks:

- `tiny_gltf.h` v2 is stable and in maintenance mode.
- `tiny_gltf_v3.h` plus `tiny_gltf_v3.c` is the intended successor, but the C
  runtime is still described by upstream as experimental and its API/behavior may
  still change while it matures.

Policy for N-Ray:

- Use TinyGLTF v2 for a production importer unless the branch explicitly accepts
  v3 churn.
- Evaluate TinyGLTF v3 in a separate dependency branch before making it the main
  asset importer.
- If using v2, define `TINYGLTF_IMPLEMENTATION` and the stb/json implementation
  macros in one translation unit only.
- Prefer preserving source image channels where needed so non-color maps are not
  accidentally widened or transformed in ways that obscure channel intent.
- Use custom URI and image callbacks if asset copying, sandboxing, or packaged
  resources need stricter control than TinyGLTF's default filesystem path.

TinyGLTF parses the container. N-Ray still owns the conversion from glTF
accessors, nodes, primitives, materials, textures, and images into renderer
upload structs.

## Import Pipeline

The importer should be staged and explicit:

1. Parse `.gltf` or `.glb` with TinyGLTF.
2. Validate required primitive attributes:
   - `POSITION` is required for renderable triangles.
   - `NORMAL` should be consumed if present; otherwise generate flat normals.
   - `TEXCOORD_0` is needed for textured PBR paths.
   - `TANGENT` should be consumed if present; otherwise generate when normal maps
     require it.
3. Decode accessors into N-Ray staging arrays, respecting component type,
   normalization, byte offsets, byte stride, sparse accessors, and index widths.
4. Apply node transforms and instance/material assignments according to the
   chosen renderer policy.
5. Split or tag primitives by material so each triangle has a stable material id.
6. Build compact traversal records, shading records, material records, and the
   flat BVH.
7. Deduplicate images by resolved source identity, then upload textures with the
   correct Vulkan format and descriptor index.
8. Allocate buffers/images through the VMA-backed Vulkan resource layer described
   in [`docs/vulkan-memory-allocator-integration.md`](vulkan-memory-allocator-integration.md).
9. Add debug views before full PBR: base color, UVs, normal map, roughness,
   metallic, material id, triangle id, and TBN handedness.

## Validation Checklist

- Load the local `assets/*/scene.gltf` validation corpus first, then add Khronos
  glTF sample assets for specific coverage gaps such as alpha mask/blend,
  emissive, occlusion, or tangent edge cases.
- Verify base color/emissive textures are sampled as sRGB and data textures are
  sampled as linear.
- Verify roughness comes from G and metallic from B.
- Verify occlusion comes from R when present.
- Compare assets with and without tangents to catch TBN generation errors.
- Add a normal-map handedness debug view before adding an importer-level Y-flip
  escape hatch.
- Compare CPU and GPU primary-hit material ids on a small scene before judging
  full path-tracing material appearance.
