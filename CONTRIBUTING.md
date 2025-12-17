# Contributing

Thanks for helping improve Jubilant DB! The following practices keep changes moving smoothly:

## Tooling expectations
- **clang-tidy is mandatory.** Resolve all clang-tidy findings. Configure with a tidy preset so clang-tidy runs against every target (library, CLI, and tests) during compilation:
  - `cmake --preset dev-debug-tidy`
  - `cmake --build --preset dev-debug-tidy`
- **clang-format is required.** Run `cmake --build --preset dev-debug --target clang-format` before sending a pull request. CI enforces formatting but will fail instead of auto-fixing.
- CMake presets drive both local and CI builds. Use `cmake --preset dev-debug` for regular builds/tests and the tidy presets for linting.
- **Skip clang-tidy only for non-semantic changes.** CI will skip the clang-tidy build if the latest commit message contains `[skip-tidy]`. Use this marker only when the commit is documentation-only or strictly non-semantic (e.g., typo fixes, comment updates, or constant tweaks). Prefer running clang-tidy locally even for trivial changes; for everything else, keep clang-tidy enabled both locally and in CI.

## Developer build workflow
The repository is intentionally preset-driven so contributors can get consistent builds locally and in CI. A typical loop is:

1. **Configure** a build tree (includes compile commands for IDEs):

   ```sh
   cmake --preset dev-debug
   ```

   For clang-tidy coverage, switch to `cmake --preset dev-debug-tidy`.

2. **Build** the full project or targeted components:

   ```sh
   cmake --build --preset dev-debug
   cmake --build --preset dev-debug --target clang-format
   ```

   Swap `dev-debug` for `dev-release` when checking optimizer-sensitive changes.

3. **Test** using CTest via the configured preset:

   ```sh
   ctest --preset dev-debug
   ```

4. **Iterate quickly** by focusing on smaller targets (e.g., `cmake --build --preset dev-debug-tidy --target unit_tests`) when debugging lint issues.

Presets live in `CMakePresets.json`; avoid hand-rolling `cmake` invocations so CI and local environments remain aligned.

## Pull requests
- Keep changes focused and documented.
- Make sure commits describe what changed and why.
