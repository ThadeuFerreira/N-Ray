#!/usr/bin/env python3
"""Generate opt-in debug SPIR-V headers for the Vulkan compute preview."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


HEADER_SHADER_RE = re.compile(r"Generated from\s+PathTracingRenderer/shaders/([A-Za-z0-9_]+\.comp)")
HEADER_SYMBOL_RE = re.compile(r"static const unsigned char\s+([A-Za-z0-9_]+)\[\]")


def run_checked(command: list[str], cwd: pathlib.Path) -> None:
    print(" ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def discover_header_metadata(header_dir: pathlib.Path) -> dict[str, tuple[str, str]]:
    metadata: dict[str, tuple[str, str]] = {}
    for header in sorted(header_dir.glob("*_spv.h")):
        text = header.read_text(encoding="utf-8", errors="ignore")
        shader_match = HEADER_SHADER_RE.search(text)
        symbol_match = HEADER_SYMBOL_RE.search(text)
        if shader_match is None or symbol_match is None:
            continue
        metadata[shader_match.group(1)] = (header.name, symbol_match.group(1))
    return metadata


def write_header(header_path: pathlib.Path, shader_path: pathlib.Path, spv: bytes, symbol: str) -> None:
    rel_shader = shader_path.as_posix()
    with header_path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("#pragma once\n\n")
        out.write("#include <cstdint>\n\n")
        out.write(
            f"// Generated debug SPIR-V from {rel_shader} with "
            "glslangValidator -gVS --target-env vulkan1.2.\n"
        )
        out.write("// This file lives under build/ and is intentionally not checked in.\n")
        out.write(f"alignas(uint32_t) static const unsigned char {symbol}[] = {{\n")
        for offset in range(0, len(spv), 12):
            chunk = spv[offset : offset + 12]
            out.write("  ")
            out.write(", ".join(f"0x{byte:02x}" for byte in chunk))
            out.write(",\n")
        out.write("};\n")
        out.write(f"static const unsigned int {symbol}_len = {len(spv)};\n")


def generate_debug_headers(repo_root: pathlib.Path) -> int:
    shader_dir = repo_root / "PathTracingRenderer" / "shaders"
    header_dir = repo_root / "PathTracingRenderer" / "src"
    output_dir = repo_root / "build" / "generated" / "vulkan_shader_debug"
    output_dir.mkdir(parents=True, exist_ok=True)

    metadata = discover_header_metadata(header_dir)
    shader_paths = sorted(shader_dir.glob("*.comp"))
    if not shader_paths:
        print(f"No compute shaders found in {shader_dir}", file=sys.stderr)
        return 1

    missing = [shader.name for shader in shader_paths if shader.name not in metadata]
    if missing:
        print("Missing checked-in header metadata for: " + ", ".join(missing), file=sys.stderr)
        return 1

    for shader_path in shader_paths:
        header_name, symbol = metadata[shader_path.name]
        spv_path = output_dir / f"{shader_path.name}.spv"
        header_path = output_dir / header_name
        rel_shader = shader_path.relative_to(repo_root)

        run_checked(
            [
                "glslangValidator",
                "-e",
                "main",
                "-gVS",
                "-V",
                "--target-env",
                "vulkan1.2",
                "-o",
                str(spv_path),
                str(rel_shader),
            ],
            repo_root,
        )
        run_checked(["spirv-val", "--target-env", "vulkan1.2", str(spv_path)], repo_root)

        spv = spv_path.read_bytes()
        if len(spv) % 4 != 0:
            print(f"{spv_path} size is not 4-byte aligned", file=sys.stderr)
            return 1
        write_header(header_path, rel_shader, spv, symbol)
        print(f"wrote {header_path.relative_to(repo_root)} ({len(spv)} bytes)")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug", action="store_true", help="generate debug SPIR-V headers")
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
        help="repository root (defaults to this script's parent)",
    )
    args = parser.parse_args()

    if not args.debug:
        parser.error("only --debug generation is supported")

    return generate_debug_headers(args.root.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
