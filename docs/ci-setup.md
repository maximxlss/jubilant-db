# CI and Merge Queue Setup Guide

Follow these steps to align repository settings with the updated CI workflow.

## Required workflow status checks
Configure your branch protection rule for the default branch to require this check:

- `CI / Validate`

If you use GitHub's merge queue, make sure the same checks are required for the merge queue configuration.

## Merge queue configuration
- Enable **Merge queue** in the repository settings for the protected branch.
- Add the `CI` workflow to the queue's required checks (the `Validate` job listed above).
- Keep "Always run required status checks" enabled so the queue waits for format verification before merging.

## Formatting expectations
- Contributors must run `cmake --build --preset dev-debug --target clang-format` locally before opening or updating a pull request.
- CI enforces formatting via `clang-format-check` and will fail if files are not formatted. No auto-commits occur in any context, including merge queues.

## Permissions
- The workflow only needs default read permissions for repository contents.

## Notes on forks
- Pull requests from forks run the same validation sequence. Since no auto-commits occur, authors must push formatted changes explicitly.
