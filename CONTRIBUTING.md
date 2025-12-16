# Contributing

Thanks for helping improve Jubilant DB! The following practices keep changes moving smoothly:

## Tooling expectations
- **clang-tidy is mandatory.** Resolve all clang-tidy findings. You can enable tidy locally with the provided CMake preset:
  - Configure/build with `cmake --preset dev-debug-tidy` (or the matching release preset) so diagnostics are emitted during compilation.
- **clang-format is preferred.** Running `cmake --build --preset dev-debug --target clang-format` keeps diffs tidy. The CI workflow also runs clang-format and will auto-commit any fixes it applies.
- CMake presets drive both local and CI builds. Use `cmake --preset dev-debug` for regular builds/tests and the tidy presets for linting.

## Tests and builds
- After configuring with a preset, build with `cmake --build --preset dev-debug` and run tests using `ctest --preset dev-debug`.
- When debugging lint issues only, you can build a smaller target (for example, `cmake --build --preset dev-debug-tidy --target unit_tests`) to speed up feedback.

## Pull requests
- Keep changes focused and documented.
- Make sure commits describe what changed and why.
- If CI reformats files, allow the automated formatting commit to land or rebase onto it before merging.
