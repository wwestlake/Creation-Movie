# Agent Instructions

## Build Output

- Always build Creation Movie in the existing `D:\000 Creation Movie\build` directory.
- Do not create alternate or scratch build folders (`build-test`, `build-clean`, or similar).
- If a different build directory ever seems necessary, stop and discuss it with the user before doing anything.
- Configure with `-DJUCE_DIR=D:\JUCE2\JUCE` so Creation Movie stays on the same JUCE checkout as Creation Station and Creation Engine.

## Shared Build Environment

- Match Creation Engine's LLVM/vcpkg discovery pattern unless there is a concrete reason to diverge.
- Prefer the already-built shared LLVM install on this machine before asking for a new LLVM rebuild.
- **Never build, rebuild, or touch LLVM — directly or as a side effect of any `vcpkg` command (including `vcpkg install` in manifest mode, which reconciles the whole dependency list) — without an explicit, in-the-moment yes from the user.** See the root `AGENTS.md`'s LLVM / vcpkg Build Rule for the full incident this is based on. If blocked, stop and ask — never act.
- **Under no circumstances write, download, build, or install anything on the C: drive. Period.** No exceptions for size or a tool's default location — redirect to a D:-drive path instead. See the root `AGENTS.md`'s C: Drive Rule.

