# Creation Movie

Creation Movie is the video-editing sibling in the Creation suite: a JUCE-based editor for timeline cutting, preview, render orchestration, titles, and future language-driven motion/media workflows.

Sibling apps:

- [Creation Station](https://github.com/wwestlake/CreationStation) — DAW
- [Creation-Engine](https://github.com/wwestlake/Creation-Engine) — game engine
- Creation Live — streaming / broadcast control

## Goals For This Scaffold

- establish the same JUCE app shell conventions as the other Creation apps
- adopt the same LLVM discovery pattern as Creation Engine
- reserve a per-app language host layer so one shared language can exist across all apps while still enforcing domain boundaries

## Layout

- `Source/` — desktop app shell and main workspace scaffold
- `Language/` — per-app language host policy for the shared Creation language
- `cmake/` — shared LLVM discovery helpers
- `docs/` — design and rollout notes

## Building

Requires CMake 3.22+, Visual Studio 17 2022, and the shared JUCE checkout already used by Creation Station and Creation Engine.

```powershell
$env:JUCE_DIR="D:\JUCE2\JUCE"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## LLVM / Language Setup

This repo uses the same additive LLVM discovery approach as Creation Engine:

- first look for `vcpkg_installed/x64-windows` in this repo
- otherwise fall back to `D:\000 Creation Engine\vcpkg_installed\x64-windows`

That lets Creation Movie share the already-built Windows LLVM install on this machine while still supporting its own manifest-local install later.

## Status

Scaffold only. The current app is a placeholder shell showing the intended workspace shape and shared-language host boundary.

