# Repository Guidelines

## Project Structure & Module Organization

This `vendor` tree contains a vendored `raylib` checkout used by the parent N-Ray project. Keep changes focused under `raylib/` unless the parent project explicitly requires a vendor refresh.

- `raylib/src/`: core C99 library sources, public headers, platform backends, and bundled external dependencies in `src/external/`.
- `raylib/examples/`: runnable sample programs grouped by module, such as `core/`, `models/`, `shaders/`, and `textures/`.
- `raylib/tools/`: utility programs, including API parsing and example validation tools.
- `raylib/projects/`: IDE and platform project templates.
- `raylib/cmake/`, `raylib/CMakeLists.txt`, `raylib/build.zig`: supported build entry points.
- `raylib/logo/` and `examples/*/resources/`: image, audio, shader, and model assets.

## Build, Test, and Development Commands

Run commands from this directory unless noted.

- `make -C raylib/src PLATFORM=PLATFORM_DESKTOP`: builds the desktop static library with the bundled desktop backend.
- `make -C raylib/src clean`: removes make-generated library outputs.
- `make -C raylib/examples core/core_basic_window`: builds one example after the library exists in `raylib/src`.
- `cmake -S raylib -B raylib/build -DBUILD_EXAMPLES=ON`: configures a CMake build including examples.
- `cmake --build raylib/build`: builds the configured CMake targets.
- `zig build --build-file raylib/build.zig`: builds through the Zig entry point when Zig is available.

## Coding Style & Naming Conventions

Follow `raylib/CONVENTIONS.md`. Use 4 spaces, no tabs, no trailing spaces, and C99-compatible code. Function and type names use `TitleCase` (`InitWindow`, `Texture2D`), variables and parameters use `lowerCase`, macros and enum values use `ALL_CAPS`, and files/directories use `snake_case`. Comments should be placed before the code they explain and start with a capital letter.

## Testing Guidelines

There is no separate unit-test suite in this vendor tree. Validate changes by building the library and the most relevant examples. For API or behavior changes, build at least one example from the affected module, for example `make -C raylib/examples textures/textures_image_loading`. Prefer adding or updating examples when a change needs executable coverage.

## Commit & Pull Request Guidelines

Keep commits small and scoped. Existing history uses short descriptive summaries, often sentence-style in N-Ray and module-prefixed in raylib, such as `rlparser: update raylib_api.* by CI`. For PRs, describe the vendor impact, list build/example validation performed, link related issues, and include screenshots only when visual behavior changes. Do not commit generated build directories or local artifacts.
