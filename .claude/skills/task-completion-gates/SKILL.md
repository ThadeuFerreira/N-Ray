---
name: task-completion-gates
description: Use for enforcing explicit build/validation completion checks before finishing tasks.
---

# Task Completion Gates

Use this skill for any task where the user expects code changes to land in a passing build state.

## Rule

- Never conclude implementation work with a compile or link failure.
- Before reporting completion, run an appropriate local build command that covers the changed target(s).
- If the build fails, fix the build errors and rebuild until the command succeeds.
- For repo tasks that involve native binaries, treat a successful compile/link as the minimum completion gate.

## Suggested workflow

1. Implement the requested change.
2. Run the relevant make/build command for the touched target.
3. Inspect output for warnings only; resolve errors before ending the task.
4. Re-run if any compile/link failure appears.

## RenderDoc and headless Vulkan work

- For docs-only RenderDoc guidance, verify links and terminology; avoid
  unnecessary builds if they would dirty vendored raylib state.
- For future headless RenderDoc implementation work, `make build-renderdoc-headless`
  and `bin/Release/NrayRenderDocHeadless --help` are the minimum gates.
- For Vulkan lighting or performance changes, run a small headless A/B render
  with fixed `--width`, `--height`, `--samples`, `--max-bounces`, and model
  selection. Compare baseline `gpuDispatchMs` against an explicit
  `--point-light-shadows` run and confirm the JSON `lighting` object matches
  the intended toggles.
- When Vulkan, RenderDoc, and the Python replay module are available, run one
  small capture plus report using the commands documented in
  `docs/headless-renderdoc-vulkan-plan.md`.
- If any of Vulkan, RenderDoc, or Python replay support is unavailable, record
  the exact blocker output and still validate CLI parsing and build/link status.

## Examples

- `make -C build config=performance_x64` after C++ changes in PathTracingRenderer.
- `make run`, `make run-performance` for end-to-end runtime checks.
- `make generate` / `make clean` only when project-file or toolchain changes require it.
