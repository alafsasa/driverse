# Contributing to Driverse

Thanks for your interest! Driverse is a safety-critical storage tool: a bug here
can destroy user data. Contributions are welcome, but the safety rules below
are non-negotiable.

## Getting started

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDRIVERSE_WERROR=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

- Compiler: C++20 (GCC 10+ / Clang 12+), Qt 6 (Core, Widgets, Concurrent).
- The build must be warning-free with `-Wall -Wextra -Wpedantic` (`-Werror`
  recommended via `-DDRIVERSE_WERROR=ON`).
- GUI tests run headless: `QT_QPA_PLATFORM=offscreen`.

## Project layout

```
src/core/       scanner, recommendations, cleanup plan/executor, protections
src/platform/   /proc mount discovery, XDG Trash
src/utils/      config, audit log, cleanup→audit bridge, logger, integration
src/gui/        Qt widgets/models (no business logic; thin over core)
tests/          CTest suites (plain assert based)
```

Core logic must stay independent of Qt Widgets (it links Qt6::Core only where
unavoidable; prefer pure std). GUI code should call into `driverse_core`
rather than growing its own logic.

## Safety rules for changes

Any change touching the scan → recommend → plan → execute pipeline must
preserve these invariants, and PRs that weaken them will be rejected:

1. **Scanning never mutates.** No write/rename/unlink in `src/core/scanner.cpp`
   or the code paths it calls.
2. **Protected paths stay protected.** `Protections` (built-ins + registry) is
   checked at plan time *and* again at execution time. Do not add a path to the
   pipeline that bypasses either check.
3. **No symlink following** during scan or deletion. `canonicalizeNoFollow` is
   deliberate: act on the link, never the target.
4. **Dry-run is mandatory first.** `executeCleanup` in `DryRun` mode must never
   touch the filesystem, even on errors.
5. **TOCTOU:** every destructive action is preceded by an `(dev, ino, type)`
   identity check against the plan; drift aborts the batch by default.
6. **Trash-first.** Permanent delete requires the explicit opt-in *and* a Safe,
   user-owned, regenerable candidate. Never add another path to permanent
   deletion without updating the confirmation gates in `cleanupdialog`.
7. **No unaudited destructive action.** Apply must remain gated on a
   successful "before" audit record.
8. **New behavior needs tests.** Safety-relevant additions belong in
   `tests/core/test_cleanup.cpp`, `tests/core/test_safety_scenarios.cpp`, or a
   new suite; GUI gating logic goes in `tests/gui/`.

## Code style

- Match the surrounding code: 4-space indent, 100-column soft limit,
  `driverse::core` / `driverse::gui` namespaces, headers with `#pragma once`.
- Prefer `std::filesystem` with `std::error_code` overloads over throwing
  variants in fallible paths.
- No new third-party dependencies without prior discussion (reproducible builds
  are a goal — see the Q1 resolution in PROJECT_PLAN.md re: Qt Charts).
- User-visible strings go through `tr()`.

## Tests

```bash
ctest --test-dir build --output-on-failure        # everything
ctest --test-dir build -R cleanup                 # one suite
./build/tests/test_safety_scenarios               # one binary directly
```

When adding a suite, register it in `tests/CMakeLists.txt` with
`add_test(NAME <short> COMMAND <binary>)`. GUI suites need
`set_tests_properties(<name> PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")`.

## Commits & branching

- One logical change per commit; follow the existing message style
  (e.g. `Phase 10: cleanup workflow (dry-run, confirm, execute)` or
  `Fix TOCTOU check for directories`).
- Keep the tree warning-free and all tests green in every commit.
- Branching strategy (from the project plan): `main` for stable,
  feature branches per phase.

## Updating the plan

`PROJECT_PLAN.md` is the source of truth for scope, status, known gaps, and
design decisions. If you complete or discover something notable, update the
phase checklist, "Known gaps", and the progress log in the same commit.
