# glTF PBR Import For The Vulkan Path

This document describes how N-Ray should import glTF 2.0 assets into the Vulkan
path while preserving physically based material intent. The current CPU path
still uses the OBJ `ObjImporter` scene, while `VulkanComputePreview` has a
flat glTF preview importer for local validation assets.

References:

- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Khronos glTF skins tutorial](https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_020_Skins.md)
- [Khronos glTF PBR overview](https://www.khronos.org/gltf/pbr)
- [TinyGLTF](https://github.com/syoyo/tinygltf)
- [Vulkan descriptor set specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)
- [Vulkan memory allocation guide](https://docs.vulkan.org/guide/latest/memory_allocation.html)
- Local reference loaders: [`tutorials/saschawillems/gltf/`](../tutorials/saschawillems/gltf/) — vendored, reference-only copies of the
  [`SaschaWillems/Vulkan`](https://github.com/SaschaWillems/Vulkan) `gltfloading` and `gltfscenerendering` examples (see
  [Reference Implementations](#reference-implementations) below).
- Upstream animated-skinning reference:
  [`SaschaWillems/Vulkan` `examples/gltfskinning`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/gltfskinning).
- Upstream hardware-ray-tracing shadow reference:
  [`SaschaWillems/Vulkan` `examples/raytracingshadows`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/raytracingshadows)
  for glTF vertex/index buffers used as both acceleration-structure inputs and
  closest-hit SSBOs, plus the two-miss-shader shadow-ray SBT pattern.

## Role In N-Ray

glTF should become the preferred asset interchange format once the Vulkan GPU
buffer contract is stable. The first compute path should still upload the
renderer-owned data that already exists today: `data.triIsect`, triangle shading
data derived from `data.tris`, `data.materials`, and `globalCompactBVH`.

The current preview importer is a validation bridge: it parses local glTF assets
with TinyGLTF, flattens them into `Tri`/`TriIntersect`/`PBRMaterial`/compact BVH
data, uploads explicit storage-buffer structs, and renders through the
progressive Vulkan compute preview. It does not replace the CPU runtime scene,
but it now evaluates textured glTF material inputs in the compute path.

For robust validation visibility, the compute preview also applies small
scene-scale ray/BVH tolerances and uploads imported triangles as two-sided. This
is a preview-path simplification for thin panels, wheels, mirrors, and similar
assets; future strict glTF raster/PBR paths should honor material `doubleSided`
state and backface-culling rules directly.

Transparency/refraction status is deliberately split:

- `alphaMode` is treated as coverage/cutout data, not physical transmission.
- `KHR_materials_transmission`, transmission textures, and `KHR_materials_ior`
  are imported into the Vulkan material SSBO and used by the compute bounce loop.
- Thin transmission is currently the reliable glass path and visibly affects the
  image without macroscopic bending.
- Volume transmission is wired with Snell refraction, single-medium tracking,
  Beer's-law attenuation, UI controls, and a preview fallback that assigns small
  scene-scaled thickness to transmissive materials when `KHR_materials_volume` is
  absent. It still needs visual tuning and validation before being considered
  finished.
- Ray-traced shadows attenuate through transmissive materials; shadow-map mode
  remains an approximation for transparent/volumetric casters.

After that contract is proven in the renderer path, a glTF importer should
replace the hardcoded `ObjImporter` scene frontend by flattening glTF meshes and
materials into the same GPU upload model. If raylib `Model` or `Mesh` loading is
used during transition, treat it as an import frontend only: copy CPU mesh data
into N-Ray's native Vulkan staging structs and do not bind raylib/OpenGL mesh
buffers to the compute renderer.

## PBR Is The Material Model, Not The Transport Algorithm

PBR does not require ray tracing or path tracing. Physically based rendering is a
local shading model: it describes how light interacts with one surface hit point.
The usual real-time model is a Cook-Torrance BRDF with GGX microfacets,
Fresnel, roughness, metalness, and energy-conserving diffuse/specular terms.

That local BRDF only needs the data available at the shaded pixel or hit point:

- `V`: view direction.
- `L`: incoming light direction.
- `N`: geometric or normal-mapped surface normal.
- Material parameters such as albedo, roughness, metalness, emission, normal
  map, and sometimes transmission or absorption.

A traditional rasterization renderer can evaluate those equations in a fragment
shader. The renderer supplies direct lights, shadow maps, reflection probes,
lightmaps, spherical harmonics, ambient cubes, or image-based lighting to
approximate the incoming light. The PBR material math can still be physically
plausible even when the global illumination source is approximated.

Ray tracing and path tracing solve a different problem: global light transport.
They ask where light came from, what it hit before this point, and where it
continues after a bounce. In a path tracer, every surface hit still evaluates a
material model like PBR. The BRDF determines how much energy is absorbed,
emitted, reflected, refracted, or scattered into the next sampled ray direction.

| Renderer abstraction | Direct lighting | Indirect GI | Reflection/refraction | Material shading math |
| --- | --- | --- | --- | --- |
| PBR rasterizer | Analytical lights and shadow maps | Approximated with IBL, probes, lightmaps, ambient cubes, or spherical harmonics | Approximated with SSR, probes, planar reflections, or cubemaps | Cook-Torrance PBR in a fragment shader |
| PBR path tracer | Rays sampled toward lights or through the scene | Traced dynamically through many bounces | Traced through recursive/specular/transmissive paths | Cook-Torrance PBR at each ray hit |

N-Ray is combining these ideas because the Vulkan target is a path tracer, and a
path tracer needs a physically plausible material model to look correct. Using a
non-physical model such as classic Blinn-Phong for bounce decisions would break
energy conservation and make indirect light, metals, rough surfaces, and glass
behave incorrectly. PBR supplies the micro-scale material interaction; path
tracing supplies the macro-scale light transport.

This also explains the staged implementation:

- The glTF importer and `PBRMaterial` conversion are renderer data work, not
  inherently ray-tracing work.
- A Vulkan graphics/raster backend could use the same imported PBR material
  factors and textures in fragment shaders.
- The current Vulkan compute preview does not use a graphics fragment stage, so
  it must evaluate material shading inside the compute shader after traversal
  finds the closest hit.
- The current glTF compute shader is still a validation step for scene
  flattening, material indexing, BVH traversal, texture sampling, progressive
  accumulation, shadows, denoising guide buffers, and transmission before full
  glTF material coverage and tuned volume refraction are complete.

## Local Validation Corpus

The top-level `assets/` directory is N-Ray's local glTF validation corpus. Use
these bundles as the first importer/PBR validation source before reaching for
external sample repositories:

- `assets/2018_garage_mak_nissan_s15_silvia_-_reggie_mah/scene.gltf`
- `assets/2024_lbsilhouette_works_murcielago_gt_evo/scene.gltf`
- `assets/accurate_torvosaurus_tanneri/scene.gltf`
- `assets/beretta_arx160/scene.gltf`
- `assets/beretta_m9_gameready/scene.gltf`
- `assets/hulk_infinity_hulk/scene.gltf`
- `assets/luna_snow_-_sonic_trailblazer/scene.gltf`
- `assets/wolverine_-_wolverine_-_x-2099_bundle/scene.gltf`

Each validation bundle should keep its `scene.gltf` or `.glb`, binary buffers,
textures, and license/source files together in the same subfolder. Folders under
`assets/` that do not contain a glTF/GLB scene file are source material only
until an importable scene entry point is added.

These assets are separate from the current `PathTracingRenderer/models/` and
`PathTracingRenderer/textures/` runtime assets used by the OBJ-based CPU path.
They should drive validation for glTF accessor decoding, PBR material mapping,
texture color-space handling, tangent generation, and Vulkan upload layout.

The preview importer also accepts skinned validation meshes. It does not upload
animated joint palettes yet; instead, it evaluates the glTF bind pose on the CPU
when a mesh node has `skin`, `JOINTS_0`, `WEIGHTS_0`, and inverse bind matrices.
For each vertex it builds the usual linear-blend skin matrix from
`jointWorld * inverseBindMatrix`, including the root node and N-Ray Z-up
conversion, then flattens the skinned bind-pose triangle into the same BVH data
as rigid meshes. This keeps assets with Blender/FBX root-axis correction nodes
upright in the current static preview while leaving runtime skeletal animation
and GPU joint-palette skinning as future work.

## glTF Skinning And Animation Contract

The current bind-pose bake is intentionally a bridge. A real animated glTF path
should follow the same structure used by the upstream Sascha Willems
`gltfskinning` sample and the Khronos glTF skin tutorial:

- Nodes must keep both hierarchy links and mutable TRS components:
  `parent`, `children`, `index`, `translation`, `rotation`, `scale`, `matrix`,
  and `skin`. Animation channels modify TRS independently, so do not collapse a
  node permanently into one matrix at load time.
- Vertices need `JOINTS_0` and `WEIGHTS_0` in addition to position, normal, UV,
  tangent, and material data. glTF supports up to four joint influences in those
  base attributes; handle normalized integer and float accessor variants
  explicitly.
- A skin owns its `joints` node list, optional `skeleton` root, inverse bind
  matrices, and a per-skin joint-matrix upload buffer. In a graphics sample this
  is usually an SSBO bound beside the mesh; in N-Ray's compute path it should be
  a scene buffer indexed from a skinned primitive or instance record.
- Animations are sampler/channel pairs. Samplers contain keyframe input times
  and output values; channels bind a sampler to a target node and path
  (`translation`, `rotation`, `scale`, or morph-target `weights`). Implement
  `LINEAR` first: use `glm::mix` for translation/scale and normalized
  `glm::slerp` for quaternion rotation. `STEP` and `CUBICSPLINE` can be added
  after the basic animated path is correct.

For a runtime joint palette, evaluate the current node globals from the animated
TRS hierarchy every frame, then build joint matrices with:

```cpp
glm::mat4 inverseTransform = glm::inverse(getNodeMatrix(meshNode));
jointMatrices[i] =
	inverseTransform *
	getNodeMatrix(skin.joints[i]) *
	skin.inverseBindMatrices[i];
```

The leading `inverseTransform` converts the joint result back into the mesh
node's local space. N-Ray's current CPU bind-pose bake omits that inverse because
it flattens final world-space triangles directly into the BVH; an animated GPU
path should keep the mesh transform and joint palette separate until shading or
vertex evaluation.

Shader-side linear blend skinning is the weighted matrix sum:

```glsl
mat4 skinMat =
	inJointWeights.x * jointMatrices[int(inJointIndices.x)] +
	inJointWeights.y * jointMatrices[int(inJointIndices.y)] +
	inJointWeights.z * jointMatrices[int(inJointIndices.z)] +
	inJointWeights.w * jointMatrices[int(inJointIndices.w)];
```

A graphics pipeline applies `projection * view * model * skinMat * position`.
For N-Ray compute/path tracing there is no vertex shader stage, so the same math
must either run on the CPU before BVH build for static validation, in a compute
deformation pass before building/refitting acceleration data, or in a future
skinned-ray intersection strategy. Do not mix these spaces casually: the common
90-degree "lying down" bug appears when rigid node transforms include root axis
corrections but skinned vertices are evaluated without the equivalent skeleton
root/global transform.

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

For that future hardware path, the official Sascha Willems `raytracingshadows`
sample is the relevant shadow reference: closest-hit reconstructs the hit
geometry from descriptor-bound glTF vertex and index buffers, traces a secondary
shadow ray against the same TLAS, and uses a second miss shader to identify
unoccluded light samples. That is a hardware RT backend pattern, not a change to
the current compute-preview buffer contract.

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

See `tutorials/saschawillems/gltf/gltfscenerendering/` for the worked example of
the `TANGENT` accessor load and this exact TBN reconstruction.

When tangents are missing and a normal texture is used, the importer should
generate tangents using MikkTSpace-compatible logic from positions, normals, and
the selected texture coordinates. The glTF specification recommends MikkTSpace
for this case. Generated tangents should be stored in the GPU shading buffer so
the path tracer does not do per-hit tangent reconstruction work.

Do not add a global "invert normal Y" default for glTF. glTF's normal convention
is OpenGL-style +Y. If N-Ray later imports non-glTF source data or artist-authored
DirectX normal bakes, carry an explicit material/import flag and flip the green
channel only for those assets.

## Reference Implementations

Two canonical tinygltf-based loaders are vendored locally under
[`tutorials/saschawillems/gltf/`](../tutorials/saschawillems/gltf/) as
reference-only material (not built; they depend on the upstream example
framework). Read them before writing or extending the importer — they are the
worked examples for the rules above. For animated skinning, use the upstream
`examples/gltfskinning` sample alongside those local files; it is not currently
vendored into N-Ray.

- `tutorials/saschawillems/gltf/gltfloading/gltfloading.cpp` (`VulkanglTFModel`,
  FlightHelmet) — minimal end-to-end loader: `LoadASCIIFromFile`, node hierarchy
  with parent-matrix accumulation, TRS-vs-`matrix` local transforms,
  POSITION/NORMAL/TEXCOORD_0 accessor decode via
  `accessor.byteOffset + view.byteOffset`, the uint32/uint16/uint8 index
  component-type switch, and embedded-image RGB→RGBA conversion.
- `tutorials/saschawillems/gltf/gltfscenerendering/` (`VulkanglTFScene`, Sponza,
  plus its tutorial `README.md`) — adds the production material layer: `TANGENT`
  load + TBN reconstruction, `doubleSided`/`alphaMode`/`alphaCutoff`, per-material
  pipelines via specialization constants, external texture loading, and node
  visibility toggling.
- Upstream `SaschaWillems/Vulkan` `examples/gltfskinning` (not currently
  vendored in this tree) — the worked animated-skinning reference: node `skin`
  links, mutable TRS node components, `JOINTS_0`/`WEIGHTS_0` vertex attributes,
  `Skin` records with inverse bind matrices and per-frame joint-matrix SSBOs,
  animation samplers/channels, `updateAnimation`, `updateJoints`, descriptor set
  binding for joint matrices, and GLSL weighted skin matrices.

Mapping to N-Ray's current importer
(`PathTracingRenderer/src/gltf_scene.cpp`): N-Ray already does node hierarchy +
TRS, all three index types, and **bounds-checked** accessor decode (stricter
than these examples), plus a Y-up→Z-up root rotation the Y-up examples do not
need. The examples are the canonical pattern for N-Ray's current **gaps** —
`TANGENT`/TBN, `TEXCOORD_0` decode, and normal/metallic-roughness/occlusion
texture sampling (today N-Ray only averages a base-color texture to a flat
albedo). When porting toward the compute path, treat them as import frontends:
flatten their per-vertex/material data into N-Ray's native traversal/shading/
material/BVH upload structs, not as graphics vertex buffers bound to compute.
See [`tutorials/saschawillems/gltf/README.md`](../tutorials/saschawillems/gltf/README.md)
for the full technique→`gltf_scene.cpp` map.

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

The importer should be staged and explicit (see
`tutorials/saschawillems/gltf/gltfscenerendering/` for the worked
primitive/material walk this mirrors):

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
