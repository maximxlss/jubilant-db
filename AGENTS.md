# Agent Guidelines

- This repository expects contributors to address all `clang-tidy` diagnostics. Use the CMake presets (e.g., `dev-debug-tidy`) to surface tidy feedback locally.
- Running `clang-format` before submitting is required; CI enforces formatting but will not auto-fix or commit changes for you.
- The CI pipeline relies on CMake presets (not raw configure flags). Use `cmake --preset dev-debug` for day-to-day builds and `cmake --preset dev-debug-tidy` for lints.
- Keep tooling configurations in `.clang-format` and `.clang-tidy` authoritative; avoid wrapping imports in try/catch blocks.
- Before making assumptions about behavior, scan the current code and documents (start with `README.md` and `CONTRIBUTING.md`) so changes align with the existing design.
- Developer build instructions live in `CONTRIBUTING.md`; user-facing build guidance is in `README.md`.
- Clang-tidy skipping is reserved for non-semantic or documentation-only commits. The CI workflow honors the `[skip-tidy]` commit marker to bypass clang-tidy, but you should prefer running clang-tidy locally and leave CI linting on for any semantic or functional change.
- For new features or significant changes, consider updating `MAIN_SPECIFICATION.md` and `TECH_SPECIFICATION.md` to reflect the intended design.
- DO NOT, I repeat DO NOT write incomplete, untested, incorrect, or one you are unsure about code and documentataion. Always strive for correctness and completeness in your contributions. Report to the user if you are unsure about any part of the task. Report to the user if you find any ambiguities or contradictions in the provided information. Report to the user if you have failed to deliver correct and complete code or documentation.
- CONTRIBUTING.md is just as important as this file is; read CONTRIBUTING.md.
- Before committing, run cmake --build --preset dev-debug --target clang-format and cmake --build --preset dev-debug-tidy; commits with formatting drift or outstanding clang-tidy warnings are not permitted.

# ExecPlans
 
When writing complex features or significant refactors, use an ExecPlan (as described in .agent/PLANS.md) from design to implementation.

## Commit guidelines (Conventional Commits)

Required format:

* `feat:`, `fix:`, `perf:`, `refactor:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`
* Breaking change uses `!` + footer.
* Skip clang-tidy only for non-semantic changes. CI will skip the clang-tidy build if the latest commit message contains [skip-tidy]. Use this marker only when the commit is documentation-only or strictly non-semantic (e.g., typo fixes, comment updates, or constant tweaks). Prefer running clang-tidy locally even for trivial changes; for everything else, keep clang-tidy enabled both locally and in CI.
