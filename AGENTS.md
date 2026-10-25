# Agent Instructions

## Repository workflow (Executive Director, October 24, 2026)

Codex is the designated working agent for the entire Creation Suite repository
structure until the Executive Director assigns it elsewhere. Do not request
ownership negotiations or handoffs from other LLMs.

Begin each assignment from clean, integrated `master`, then create a fresh
task-specific branch. Preserve unfinished work. Complete the implementation,
build and focused checks; commit and publish a PR targeting `master` for the
Executive Director to review/merge. Do not bypass required reviews/checks.
Only after a successful merge, delete the completed branch, update the umbrella
gitlink through its PR process, and verify clean repositories. An open PR is
ready for review, not an integrated milestone. Do not start the next milestone
before its prerequisite work is merged.

## Build Output

- Build in `build/` inside this shared CreationMovie checkout. The old `D:\000 Creation Movie` checkout was retired in the October 17 workspace consolidation.
- Do not create alternate or scratch build folders (`build-test`, `build-clean`, or similar).
- If a different build directory ever seems necessary, stop and discuss it with the user before doing anything.
- Configure with `-DJUCE_DIR=D:\JUCE2\JUCE` so Creation Movie stays on the same JUCE checkout as Creation Station and Creation Engine.

## Shared Build Environment

- Match Creation Engine's LLVM/vcpkg discovery pattern unless there is a concrete reason to diverge.
- Prefer the already-built shared LLVM install on this machine before asking for a new LLVM rebuild.
- **Never build, rebuild, or touch LLVM — directly or as a side effect of any `vcpkg` command (including `vcpkg install` in manifest mode, which reconciles the whole dependency list) — without an explicit, in-the-moment yes from the user.** See the root `AGENTS.md`'s LLVM / vcpkg Build Rule for the full incident this is based on. If blocked, stop and ask — never act.
- **Under no circumstances write, download, build, or install anything on the C: drive. Period.** No exceptions for size or a tool's default location — redirect to a D:-drive path instead. See the root `AGENTS.md`'s C: Drive Rule.

