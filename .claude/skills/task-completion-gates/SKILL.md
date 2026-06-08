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

## Examples

- `make -C build config=performance_x64` after C++ changes in PathTracingRenderer.
- `make run`, `make run-performance` for end-to-end runtime checks.
- `make generate` / `make clean` only when project-file or toolchain changes require it.
