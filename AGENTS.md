# Agent Instructions

## Build Output

- Build Creation Movie in this repo's own `build/` directory.
- Do not point builds at the user's personal clones or another agent's build tree.
- Use the repo-root presets or `scripts/Build-Suite.ps1` from the umbrella repo when possible so all agents configure this app the same way.
- Set `JUCE_DIR` in the environment or pass `-DJUCE_DIR=<path-to-JUCE>` explicitly when configuring.

## Shared Build Environment

- Match Creation Engine's LLVM/vcpkg discovery pattern unless there is a concrete reason to diverge.
- Prefer the already-built shared LLVM install on this machine before asking for a new LLVM rebuild.

