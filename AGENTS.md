# Agent Guidelines

- This repository expects contributors to address all `clang-tidy` diagnostics. Use the CMake presets (e.g., `dev-debug-tidy`) to surface tidy feedback locally.
- Running `clang-format` before submitting is preferred; CI applies `clang-format` automatically and commits any fixes that occur during the workflow.
- The CI pipeline relies on CMake presets (not raw configure flags). Use `cmake --preset dev-debug` for day-to-day builds and `cmake --preset dev-debug-tidy` for lints.
- Keep tooling configurations in `.clang-format` and `.clang-tidy` authoritative; avoid wrapping imports in try/catch blocks.
