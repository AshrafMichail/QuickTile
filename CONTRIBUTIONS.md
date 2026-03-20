# Contributing to QuickTile

This repository is a Windows-only native C++ project.

## Development Setup

- Use Windows 11.
- Install Visual Studio with C++ desktop development tools.
- Use the repository root as the working directory when running scripts.

## Expected Validation

Run these commands before opening a pull request:

```powershell
./build.ps1
./build.ps1 -Analyze -ClangTidy
```

Both commands are expected to complete successfully.

## Change Scope

- Keep changes focused and easy to review.
- Prefer root-cause fixes over surface-level patches.
- Add or update tests when behavior changes.
- Update README or settings documentation when defaults, shortcuts, or configuration keys change.

## Pull Requests

- Describe the problem being solved.
- Summarize the user-visible behavior change.
- Mention any follow-up work that remains out of scope.
- Include screenshots or short recordings for UI changes when practical.

## Style

- Match the existing code style and naming patterns.
- Avoid unrelated refactors in the same change.
- Keep public-facing docs aligned with the current implementation.