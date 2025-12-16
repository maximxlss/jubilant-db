# Agent Guidelines

- This repository expects contributors to address all `clang-tidy` diagnostics. Use the CMake presets (e.g., `dev-debug-tidy`) to surface tidy feedback locally.
- Running `clang-format` before submitting is preferred; CI applies `clang-format` automatically and commits any fixes that occur during the workflow.
- The CI pipeline relies on CMake presets (not raw configure flags). Use `cmake --preset dev-debug` for day-to-day builds and `cmake --preset dev-debug-tidy` for lints.
- Keep tooling configurations in `.clang-format` and `.clang-tidy` authoritative; avoid wrapping imports in try/catch blocks.
- Before making assumptions about behavior, scan the current code and documents (start with `README.md` and `CONTRIBUTING.md`) so changes align with the existing design.
- Developer build instructions live in `CONTRIBUTING.md`; user-facing build guidance is in `README.md`.
- For new features or significant changes, consider updating `MAIN_SPECIFICATION.md` and `TECH_SPECIFICATION.md` to reflect the intended design.
- DO NOT, I repeat DO NOT write incomplete, untested, incorrect, or one you are unsure about code and documentataion. Always strive for correctness and completeness in your contributions. Report to the user if you are unsure about any part of the task. Report to the user if you find any ambiguities or contradictions in the provided information. Report to the user if you have failed to deliver correct and complete code or documentation.

# ExecPlans
 
When writing complex features or significant refactors, use an ExecPlan (as described in .agent/PLANS.md) from design to implementation.

## Commit guidelines (Conventional Commits)

Required format:

* `feat:`, `fix:`, `perf:`, `refactor:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`
* Breaking change uses `!` + footer.

