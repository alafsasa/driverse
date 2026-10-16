# Driverse — Linux Storage Cleaner

Driverse is a safe, conservative Linux desktop application for understanding disk
usage and reclaiming space. It scans a filesystem or folder, shows the largest
files and directories, and proposes cleanup candidates with explicit risk
levels — but never deletes anything by surprise.

**Core principle: safety first.** Scanning is always read-only. Removing files
requires a reviewed plan, explicit confirmations, and (for user data) a trip to
the trash instead of a delete.

## Features

- **Dashboard** — mounted filesystems with capacity/usage, a usage donut, and
  total/used/free/estimated-recoverable cards.
- **Background scans** — cancellable, progress-reporting, hard-link aware, and
  bind-mount-cycle safe. Never follows symlinks; stays on one filesystem.
- **Results view** — sortable, filterable tables of the largest files, largest
  directories (hard-link deduplicated), and cleanup recommendations.
- **Conservative recommendations** — thumbnails, browser caches, package caches,
  temp dirs, build artifacts, old downloads, logs, and generic app caches are
  classified with a risk level (Safe → SystemProtected), a category, a
  justification, and a confidence level. Name-only heuristics are never enough.
- **Guarded cleanup workflow** — dry-run preview, per-risk confirmation gates,
  type-to-confirm for irreversible actions, TOCTOU identity checks, and an
  audit log written *before* anything is touched.
- **Trash-first** — normal items are moved to the XDG Trash (recoverable).
  Permanent deletion is opt-in, and only ever offered for user-owned,
  regenerable caches classified Safe.
- **Transparency** — skipped paths are listed with reasons; failures carry
  actionable hints (disk full, read-only filesystem, busy files, ...).

## Requirements

- Linux desktop (KDE / GNOME / XFCE tested paths)
- CMake 3.20+
- C++20 compiler (GCC 10+, Clang 12+)
- Qt 6 (Core, Widgets, Concurrent) — e.g. `qt6-base-dev` on Debian/Ubuntu,
  `qt6-base` on Arch
  - *Optional:* `qt6-tools`-provided Qt Test is **not** required; the test
    suites use plain `assert` + CTest.

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/driverse
```

With warnings-as-errors (used in development):

```bash
cmake -S . -B build -DDRIVERSE_WERROR=ON
cmake --build build
```

Install (binary, `.desktop` entry, icon):

```bash
cmake --install build --prefix ~/.local
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

20 CTest targets cover the core logic (`types`, `utils`, `mounts`, `scanner`,
`scanner_userns`, `recommendations`, `cleanup`, `trash`, `safety_scenarios`),
utilities (`config`, `audit`, `cleanupaudit`, `integration`, `logger`), and the
GUI models/dialogs (`devicetablemodel`, `scanmodel`, `scanworker`,
`cleanupdialog`, `skippedpathsdialog`, `formatting`). The `scanner_userns`
suite re-runs the scanner under `unshare -rm` to exercise a real bind-mount
cycle when user namespaces are available (skipped otherwise).

The GUI suites run headless via `QT_QPA_PLATFORM=offscreen`; the same works for
a smoke run of the app:

```bash
QT_QPA_PLATFORM=offscreen ./build/driverse
```

## Data & configuration locations

| What | Location |
|---|---|
| Config | `$XDG_CONFIG_HOME/driverse/driverse.conf` (else `~/.config/driverse/driverse.conf`) |
| Audit log (JSONL) | `$XDG_DATA_HOME/driverse/audit.jsonl` (else `~/.local/share/driverse/audit.jsonl`) |
| App log | `~/.local/share/driverse/driverse.log` |
| Trash | standard XDG Trash (`~/.local/share/Trash`) |

Config example:

```ini
[general]
min_age_safe_days = 7
allow_permanent_delete = true

[protected]
paths = /boot, /efi, /usr, /etc, /srv/data
```

Protected paths added here are enforced by both the scanner and the cleanup
executor (in addition to the built-in denylist).

## Safety model

1. **Scan** — read-only; symlinks are never followed; one filesystem per scan;
   protected and pseudo-filesystem paths are skipped and reported.
2. **Recommend** — conservative classification with risk levels. Protected
   paths are `SystemProtected` and can never be selected for deletion.
   Non-user-owned files are never below `High` risk.
3. **Plan** — `buildCleanupPlan()` re-checks protection, refuses home/root and
   ancestors of home, and refuses non-user-owned items.
4. **Confirm** — the cleanup dialog shows the plan, requires an acknowledgement
   for every run, a review checkbox for `High` risk items, and typing `DELETE`
   for irreversible cases. Apply is refused if the audit "before" record cannot
   be written.
5. **Execute** — every item is re-validated (protection + `(dev, ino, type)`
   identity) immediately before acting. Any identity mismatch aborts the batch
   by default. User items go to the XDG Trash; permanent delete happens only
   for opted-in Safe regenerable caches.
6. **Audit** — previews, applies, and failures are appended to
   `audit.jsonl` (machine-parseable JSON Lines).

See [USER_GUIDE.md](USER_GUIDE.md) for a walk-through and
[PROJECT_PLAN.md](PROJECT_PLAN.md) for the full roadmap, known gaps, and
technical design notes.

## Packaging

The build installs a `.desktop` entry and a scalable icon. A source/binary
tarball can be produced with CPack:

```bash
cd build && cpack
```

(AppImage/Deb/Flatpak packaging is future work — see the project plan.)

## License

To be determined (all rights reserved until a license is chosen).
