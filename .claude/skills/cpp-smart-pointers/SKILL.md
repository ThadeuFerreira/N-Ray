---
name: cpp-smart-pointers
description: Apply modern C++ ownership rules to this renderer — smart pointers (unique_ptr/shared_ptr/weak_ptr) for heap ownership at the asset/management boundary, but raw references and integer handles inside hot per-ray/per-pixel/per-draw loops. Use when adding heap-owned or polymorphic objects, designing an asset/resource registry, reviewing a buffer/texture/mesh lifetime, or auditing a render path for hidden allocations and atomic refcount traffic.
---

# C++ smart pointers for a high-performance renderer

The goal is memory safety **and** stable frame times. Achieve both by deciding ownership at *layer boundaries*, not per object. Smart pointers belong where lifetime is managed; hot loops see only contiguous data, raw references, and indices.

## The one rule

**Own at the boundary, borrow in the loop.**
- Ownership/management layer (asset registry, resource manager, scene load): smart pointers or value containers.
- Hot path (BVH traversal, `rayLogic`, `rayGeneration`, framebuffer/tonemap loops, draw submission): raw `const T&`, `T*`, `std::vector<T>`, or `uint32_t` handles. **No `shared_ptr` copies, no allocations, no refcount traffic here.**

## Ownership decision table

| Scenario | Use | Why |
| --- | --- | --- |
| Homogeneous bulk data (triangles, rays, framebuffer) | `std::vector<T>` (contiguous) + `uint32_t` index handles | Cache-friendly sequential access; RAII; no per-element heap allocs. **This is already how N-Ray stores `tris`/`models`/BVH.** |
| Single heap-owned or polymorphic object (one owner) | `std::unique_ptr<T>` | Pointer-sized, no atomics, frees on scope exit. |
| Asset registry keyed by name/UUID | `std::unordered_map<Key, std::unique_ptr<T>>`, hand out `T*` via `.get()` | One owner; consumers borrow without lifetime control. |
| Genuinely shared, lifetime-ambiguous resource | `std::shared_ptr<T>` — only at the management layer | Atomic refcount + heap control block; never copy it inside a frame loop. |
| Break a `shared_ptr` ownership cycle / cache that may expire | `std::weak_ptr<T>` + `.lock()` | Non-owning; `.lock()` returns null if the object is gone. |
| Scene node / ECS component referencing an asset | `uint32_t` handle (slot + generation), not a pointer | Decouples layout; survives reallocation; trivially copyable. |

## Mechanics that matter for performance

- `std::unique_ptr<T>` — same size as a raw pointer (8 bytes on 64-bit), no per-operation overhead vs manual `new`/`delete`. Moving transfers ownership; it cannot be copied. (`std::make_unique` still performs **one** heap allocation — "zero overhead" means no overhead *beyond* that allocation, not zero allocation.)
- `std::shared_ptr<T>` — two pointers (object + control block). The control block is a separate heap allocation holding an **atomic** strong and weak count. Every copy/destroy is an atomic increment/decrement → cross-core cache-line synchronization. `make_shared` fuses object+control block into one allocation but the atomics remain. **A `shared_ptr<Texture>`/`shared_ptr<Mesh>` copied per entity into a frame loop causes hardware stalls and micro-stutter.**
- `std::weak_ptr<T>` — observes without bumping the strong count; prevents leaks from reference cycles; access only through `.lock()`.
- Many scattered `new`/individual allocations → random heap addresses → L1/L3 cache misses when iterated. Prefer one contiguous buffer over thousands of separate allocations.

## How to apply (the procedure)

1. **Identify the layer.** Is this code owning/loading a resource, or consuming it in a render pass?
2. **Boundary code:** replace raw `new`/`delete` and naked owning pointers with `std::unique_ptr` (single owner) or a value container. Reach for `shared_ptr` only when ownership is truly shared, and `weak_ptr` to break cycles. Add `#include <memory>`.
3. **Hot path:** ensure it receives `const T&` / `T*` / `std::vector<T>&` / indices — extract the underlying pointer once via `.get()` at the boundary, never per iteration. Audit the loop body for any `shared_ptr` by value, hidden temporaries, or per-iteration allocation.
4. **Verify:** build and confirm no behavior change; for hot paths, confirm no new allocation or atomic op was introduced inside the loop.

## N-Ray specifics

- N-Ray today uses **no** smart pointers and **no** raw `new`/`delete` — bulk data is `std::vector` + `uint32_t` handles (`tri.idx`, `modelIdx`, BVH `children[]`, `CompactBVH.startIndex`/`missLink`), and the hot paths (`traverseFlatBVH`, `rayLogic`, `rayGeneration`, the tonemap loop) take raw references/arrays. **Keep it that way** — it already embodies "borrow in the loop."
- The async render worker copies a `std::vector<Tri>` snapshot once per render *start* (off the hot path, into a worker thread) and hands frames over by buffer, not by smart pointer — correct.
- If you later add **polymorphic materials, an asset/texture registry, or optional heap-owned subsystems**, that is where `std::unique_ptr` (and a registry handing out `.get()` raw pointers) belongs — never `shared_ptr` reaching into the per-ray/per-pixel loop.
- raylib resources (`Texture2D`, `Image`) are C handles freed via `UnloadTexture`/`UnloadImage`; don't wrap them in smart pointers unless you write a custom deleter — keep the existing explicit `shutdown` calls.

## Reference architecture

```cpp
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

struct TextureResource {
    uint32_t hardware_id = 0;
    std::string source_path;
    ~TextureResource() { /* UnloadTexture / glDeleteTextures(hardware_id) */ }
};

// ASSET REGISTRY — single dedicated allocation per asset, one explicit owner.
class AssetRegistry {
    std::unordered_map<std::string, std::unique_ptr<TextureResource>> texture_pool;
public:
    void LoadTexture(const std::string& name, uint32_t gpu_id) {
        auto tex = std::make_unique<TextureResource>();   // one heap allocation
        tex->hardware_id = gpu_id;
        tex->source_path = name;
        texture_pool[name] = std::move(tex);              // explicit ownership transfer
    }
    // Hand a non-owning raw pointer to the hot loop — zero refcount overhead.
    TextureResource* Get(const std::string& name) {
        auto it = texture_pool.find(name);
        return it != texture_pool.end() ? it->second.get() : nullptr;
    }
};

// SCENE / COMPONENT — non-owning observer (or a uint32_t handle).
struct RenderPacket {
    TextureResource* bound_texture = nullptr;
    float world_matrix[16]{};
};

// HOT PASS — contiguous array, raw access only, no allocations, no atomics.
void DrawRenderQueue(const std::vector<RenderPacket>& queue) {
    for (const RenderPacket& p : queue) {
        if (p.bound_texture) {
            // glBindTexture(GL_TEXTURE_2D, p.bound_texture->hardware_id);
        }
    }
}
```

## Anti-patterns to flag

- `std::shared_ptr<T>` passed/copied by value into a per-frame, per-entity, per-ray, or per-pixel loop.
- `shared_ptr` where a single owner suffices (use `unique_ptr`).
- A pointer stored in an ECS/scene component where a `uint32_t` handle would decouple layout and survive reallocation.
- Thousands of individual `new`/`make_unique` objects iterated sequentially instead of one contiguous `std::vector`.
- `.lock()` results dereferenced without a null check.
