# Djehuti Movie

Djehuti Movie is the Creation Suite's Windows video editor. It uses JUCE for the desktop workspace, FFmpeg for media decoding and encoding, and the suite VFS for project and imported-media storage.

The current foundation provides source/program preview, timeline placement and trimming, splitting and ripple deletion, title overlays, gain and opacity, undo/redo, and background export. The shared Virtual Engineer and authenticated localhost API invoke the same commands as the application UI.

This is an evolving editor. See [Capabilities](docs/CAPABILITIES.md) for current limits, job semantics, and the focused reliability check. It is not yet a finished professional editing or distribution package.

## Shared workspace and build

Use the single shared checkout and its `build/` directory. Follow this repo's `AGENTS.md` and the umbrella instructions. Build the application incrementally, with one worker:

```powershell
cmake --build build --config Debug --target CreationMovieApp -- /m:1
```

The executable is `build/CreationMovieApp_artefacts/Debug/Djehuti Movie.exe`. The existing CMake configuration uses the suite JUCE checkout and a pinned shared FFmpeg SDK; its DLLs are copied alongside the development executable. Do not recreate the build or run dependency installers as a routine verification step. LLVM changes require separate authorization under the suite rules.

## Storage and automation

Run the shared suite VFS service using the configured suite storage root. Movie projects contain a versioned `Project/movie-state.json` entry and imported media in that project container. External files enter through import; exports go to a user-selected destination. There is no second local project-file authority.

The API discovery record is the VFS suite entry `agents/movie.json`. It contains the current loopback address and bearer token; do not publish that token. Discover command schemas through `/v1/commands`. Submit commands through `/v1/commands/<name>` and inspect the returned request through `/v1/requests/<requestId>`.

For create, open, save, import, and export, command acceptance returns a `jobId`. Wait for the matching job in `/v1/app/jobs` to reach a terminal status. `completed` means the result has also been applied to the document. See the job contract in [Capabilities](docs/CAPABILITIES.md).

## Other documentation

- [Product vision](docs/CREATION_MOVIE_VISION.md)
- [Language integration boundaries](docs/LANGUAGE_ROLLOUT.md)
- [Current implementation handoff](HANDOFF.md)
