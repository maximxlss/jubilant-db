# CI and Merge Queue Setup Guide

Follow these steps to align repository settings with the updated CI workflow.

## Required workflow status checks
Configure your branch protection rule for the default branch to require these checks:

- `CI / Format`
- `CI / Build, Test, and clang-tidy`

If you use GitHub's merge queue, make sure the same checks are required for the merge queue configuration.

## Merge queue configuration
- Enable **Merge queue** in the repository settings for the protected branch.
- Add the `CI` workflow to the queue's required checks (the two jobs listed above).
- Keep "Always run required status checks" enabled so the queue waits for the format job to pass or fail before merging.

## Auto-formatting behavior
- The format job auto-commits changes for pushes and same-repo pull requests when formatting is needed. Any auto-commit cancels downstream jobs so a fresh run can validate the new commit.
- Merge queue runs never auto-commit; they fail if formatting would be required so you can re-queue after formatting.
- The workflow ignores documentation-only changes (`docs/**` and `**/*.md`).

## Permissions
- Ensure the **GITHUB_TOKEN** has `contents: write` permission (set at the repository or organization level, or allow workflows to grant write permissions). This is required for the auto-commit step on eligible branches.

## Notes on forks
- Pull requests from forks will run the format check but skip auto-commits for safety. Authors should apply `clang-format` locally (or push from a branch in the main repository) before requesting review or merging.
