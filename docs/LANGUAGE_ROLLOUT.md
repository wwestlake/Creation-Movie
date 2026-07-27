# Creation Movie Language Rollout

Creation Movie will use the shared Creation language, but only through movie-safe domains and intrinsics.

Planned domain gates:

- allowed: `shared`, `movie`, `timeline`, `render`
- blocked: `gameplay`, `world`, `physics`, `instrument`, `mixer`, `broadcast`

Planned movie-specific layers:

1. timeline query and edit intrinsics
2. title / caption graph helpers
3. render job orchestration
4. asset tagging, search, and conform helpers

The scaffold enforces the policy boundary now with `Language/AppLanguagePolicy.*`, even before the full compiler/runtime is wired in.

