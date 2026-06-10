#!/usr/bin/env python3
"""Regenerate the checked-in release SPIR-V header for vulkan_gltf_flat.comp.

Mirrors the manual `glslc --target-env=vulkan1.2` recipe that produced
PathTracingRenderer/src/vulkan_gltf_flat_comp_spv.h. Run from the repo root.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADER = ROOT / "PathTracingRenderer" / "shaders" / "vulkan_gltf_flat.comp"
HEADER = ROOT / "PathTracingRenderer" / "src" / "vulkan_gltf_flat_comp_spv.h"
SYMBOL = "nray_vulkan_gltf_flat_comp_spv"
REL_SHADER = "PathTracingRenderer/shaders/vulkan_gltf_flat.comp"


def main() -> int:
    spv_path = ROOT / "build" / "vulkan_gltf_flat.comp.spv"
    spv_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["glslc", "--target-env=vulkan1.2", str(SHADER), "-o", str(spv_path)],
        cwd=ROOT,
        check=True,
    )
    spv = spv_path.read_bytes()
    if len(spv) % 4 != 0:
        print(f"{spv_path} size not 4-byte aligned", file=sys.stderr)
        return 1

    lines = [f"// Generated from {REL_SHADER} with glslc."]
    lines.append(f"alignas(uint32_t) static const unsigned char {SYMBOL}[] = {{")
    body = ["0x%02x" % b for b in spv]
    for off in range(0, len(body), 12):
        lines.append("  " + ", ".join(body[off : off + 12]) + ("," if off + 12 < len(body) else ""))
    lines.append("};")
    lines.append(f"static const unsigned int {SYMBOL}_len = {len(spv)};")
    HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {HEADER.relative_to(ROOT)} ({len(spv)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
