# Driverse Linux Storage Cleaner - Project Plan

This document outlines the complete project plan for building Driverse, a safe and conservative Linux storage cleaner. It breaks the work down into phases with clear deliverables, tasks, and acceptance criteria.

## Project Overview

- **Name:** Driverse (Linux Storage Cleaner)
- **Language:** C++20
- **GUI Framework:** Qt 6 (Widgets + Charts)
- **Build System:** CMake 3.20+
- **Target Platform:** Linux desktop (KDE, GNOME, XFCE)
- **Version:** 1.0.0
- **Core Principle:** Safety-first - conservative recommendations, explicit approval, protection of system files

## Status Snapshot

_Last updated: 2026-10-16_

| Phase | Title | Status |
|---|---|---|
| 0 | Foundation & Environment Setup | ✅ Complete |
| 1 | Core Data Models & Types | ✅ Complete |
| 2 | Filesystem Safety & Mount Detection | ✅ Complete |
| 3 | Filesystem Scanner Implementation | ✅ Complete |
| 4 | Device Discovery & Dashboard Data | ✅ Complete (data layer) |
| 5 | Cleanup Recommendation Engine | ✅ Complete |
| 6 | Application Storage Analysis | ⬜ Pending (optional) |
| 7 | Core Logic Integration & Utilities | ✅ Complete |
| 8 | Qt GUI - Dashboard & Device Selection | ✅ Complete |
| 9 | Results View - Files, Dirs, & Recommendations | ✅ Complete |
| 10 | Cleanup Workflow (Dry-Run, Confirm, Execute) | ✅ Complete |
| 11 | Polish, Error Handling & Edge Cases | ✅ Complete (2026-10-16) |
| 12 | Testing, Documentation & Packaging | ⬜ Next |

**What exists today:** buildable C++20/Qt6/CMake project; core static library
(`driverse_core`) with types, utilities, protections, mount detection, a
hard-link-aware, bind-mount-cycle-safe filesystem scanner, a conservative
cleanup recommendation engine, and the guarded cleanup executor (`buildCleanupPlan`
/ `executeCleanup`) plus a dependency-free XDG Trash implementation; a
cross-cutting `src/utils/` module (INI config, JSONL audit log, cleanup→audit
bridge, leveled logger, config→core integration); and a `src/gui/` dashboard
(device table, usage donut, summary cards, background cancellable scans) with a
filterable/sortable results view (largest files, largest directories, and
checkable recommendations), a details panel, and the cleanup workflow dialog
(plan summary + risk breakdown, item review table, risk-based confirmation
gates, dry run, apply with trash-first + opt-in permanent delete for Safe
caches). Phase 11 added the polish layer: scan-target validation
(unmounted/non-directory/unreadable targets), a network-filesystem warning,
actionable ENOSPC/EBUSY/EROFS/EACCES error hints in cleanup + trash, a
skipped-paths dialog, empty-state messages per tab, keyboard shortcuts
(Ctrl+F, Ctrl+1/2/3), tooltips/help text, locale-aware size and date formatting,
and a single shared risk-colour palette. CTest suite (19 targets: types, utils,
mounts, scanner, scanner_userns, recommendations, cleanup, trash, config, audit,
cleanupaudit, integration, logger, devicetablemodel, scanmodel, scanworker,
cleanupdialog, skippedpathsdialog, formatting) all passing with zero compiler
warnings.

## Architecture Goals

- **Separation of concerns:** Core logic (scanner, analyzer) independent from GUI
- **Thread-safe:** Background scanning with progress/cancellation
- **Safety-first:** Dry-run by default, risk classification, audit logging
- **Cross-platform awareness:** Linux-specific where needed (mounts, package managers)
- **Testable:** Unit tests for core logic, integration tests for safety

## Project Structure

```
driverse/
├── CMakeLists.txt          # top-level build; driverse_core lib + driverse app
├── src/
│   ├── core/               # Scanner, analysis, recommendations (no Qt Widgets)
│   │   ├── types.h/.cpp        # FileItem, DeviceInfo, ScanResult, enums
│   │   ├── utils.h/.cpp        # formatting, path protection, fs classification
│   │   ├── protections.h/.cpp  # protected/sensitive path registry
│   │   ├── scanner.h/.cpp      # thread-confined filesystem scanner
│   │   ├── recommendations.h/.cpp  # conservative cleanup classifier
│   │   └── cleanup.h/.cpp      # plan/execute: TOCTOU checks, trash-first, reports
│   ├── platform/           # Linux-specific: mounts, package managers, trash
│   │   ├── mounts.h/.cpp       # /proc mount discovery + statvfs capacity
│   │   └── trash.h/.cpp        # XDG Trash spec implementation (recoverable moves)
│   ├── utils/              # cross-cutting: config, audit, logging, integration
│   │   ├── config.h/.cpp       # INI config (XDG-aware)
│   │   ├── audit.h/.cpp        # append-only JSONL audit log
│   │   ├── cleanupaudit.h/.cpp # CleanupPlan/Report -> audit entries bridge
│   │   ├── logger.h/.cpp       # leveled logger (console + file sink)
│   │   └── integration.h/.cpp  # Config -> core options bridge
│   ├── gui/                # Qt UI: dashboard, results view, scan worker
│   │   ├── mainwindow.h/.cpp        # QMainWindow: split view, tabs, menus
│   │   ├── deviceTableModel.h/.cpp  # QAbstractTableModel for filesystems
│   │   ├── scanmodel.h/.cpp         # filterable/sortable results table model
│   │   ├── scandata.h               # immutable per-scan snapshot (shared_ptr)
│   │   ├── storageChart.h/.cpp      # custom QPainter usage donut
│   │   ├── summaryCard.h/.cpp       # dashboard summary card widget
│   │   ├── cleanupdialog.h/.cpp     # dry-run preview + confirmation gates + apply
│   │   ├── skippedpathsdialog.h/.cpp # read-only list of skipped paths + reasons
│   │   ├── formatting.h             # shared locale-aware sizes/dates + risk palette
│   │   └── scanworker.h/.cpp        # QtConcurrent scan + queued signals
│   └── main.cpp            # Qt application entry point
├── tests/                  # CTest suites (core/*, utils/*, gui/*)
├── data/                   # Resources, icons                 (future)
├── docs/                   # Additional docs                  (future)
└── scripts/                # Build/test scripts
```

## Phase 0: Foundation & Environment Setup
**Goal:** Set up build system, project structure, and development environment.  
**Duration:** ~1-2 hours  
**Status:** ✅ Complete

### Deliverables
- [x] CMakeLists.txt with Qt6 dependencies configured
- [x] Basic project directory structure
- [x] `.gitignore` appropriate for C++/Qt/CMake
- [x] Basic main.cpp with empty Qt app
- [x] Build compiles successfully

### Tasks
1. **Create project skeleton**
   - [x] Set up src/core, src/gui, src/platform, src/utils, tests
   - [x] Add initial CMakeLists.txt

2. **Configure dependencies**
   - [x] Find Qt6 (Core, Widgets, Concurrent) — `Charts` not required (Q1: custom donut)
   - [x] Configure compiler flags: `-Wall -Wextra -Wpedantic`
   - [x] Set C++20 standard

3. **Basic app bootstrap**
   - [x] Create minimal Qt application
   - [x] Verify build with `cmake` + `make/ninja`
   - [x] Add CI placeholder or local build script

### Acceptance Criteria
- Project builds cleanly with no warnings
- `./driverse` launches empty window
- CMake configuration successful

---

## Phase 1: Core Data Models & Types
**Goal:** Define data structures for devices, files, scan results, and risk classification.  
**Duration:** 1-2 days  
**Status:** ✅ Complete

### Deliverables
- [x] `FileItem` struct (path, size, inode, dev, mtime, type, flags)
- [x] `DeviceInfo` struct (mount point, device, fs type, total/used/free)
- [x] `ScanResult` container
- [x] `RiskLevel` enum and `CleanupCandidate` struct
- [x] Unit tests for data models

### Tasks
1. **File metadata model**
   - [x] Store logical size, allocated size
   - [x] Store (dev, ino) for hard-link deduplication
   - [x] Flags: isSymlink, isDir, isHardLinked, isProtected, isSparse
   - [x] Category and risk level

2. **Device detection types**
   - [x] Mount info structure
   - [x] Capacity calculation helpers

3. **Recommendation types**
   - [x] Risk taxonomy: Safe, Low, Medium, High, SystemProtected
   - [x] Confidence level (High/Med/Low) for estimates
   - [x] Justification/reason for each recommendation

### Acceptance Criteria
- Data models are complete and documented
- Copy/move semantics correct
- Unit tests pass

---

## Phase 2: Filesystem Safety & Mount Detection
**Goal:** Implement safe filesystem traversal and device discovery with protection against dangerous paths.  
**Priority:** **CRITICAL** (Safety)  
**Duration:** 2-3 days  
**Status:** ✅ Complete

### Deliverables
- [x] Mount point discovery (parse `/proc/mounts` and/or `/etc/mtab`)
- [x] Filesystem boundary detection (device ID tracking)
- [x] Protected paths system (denylist + detection)
- [x] Pseudo-filesystem exclusion (proc, sys, dev, tmpfs, fuse, overlay)
- [x] Symlink handling (`AT_SYMLINK_NOFOLLOW` by default)
- [x] Loop/cycle detection incl. bind-mount cycles (G1 — recursion-stack `(dev, ino)` guard)
- [x] One-filesystem traversal

### Tasks
1. **Mount detection** (`src/platform/mounts.*`)
   - [x] Parse mount info from `/proc/self/mountinfo` or `/proc/mounts`
   - [x] Get device major/minor, fs type, mount options
   - [x] Classify: local, network (nfs/smb), virtual (tmpfs/proc/sys)
   - [x] Filter out pseudo-filesystems by default

2. **Path protection** (`src/core/protections.*`)
   - [x] Define hard blacklist: `/boot`, `/efi`, `/usr`, `/etc`, `/var/lib`, `/proc`, `/sys`, `/dev`, `/run`, `/snap`
   - [x] Define sensitive dirs with warnings (e.g. `/home/*/.config`, but allow caches)
   - [x] Check if path is under protected root
   - [x] Check if path is system-critical
   - [x] Configurable with reset to defaults

3. **Traversal safety** (`src/core/scanner.*`)
   - [x] POSIX `opendir`/`readdir` + `fstatat(AT_SYMLINK_NOFOLLOW)` traversal
   - [x] Never follow symlinks when scanning
   - [x] Track visited directories on the recursion stack to detect bind-mount loops (G1 guard)
   - [x] Enforce device boundary: only traverse same `st_dev` as root
   - [x] Skip cross-filesystem unless explicitly requested
   - [x] Handle permission errors gracefully (collect skipped list)

### Acceptance Criteria
- Cannot traverse into `/proc`, `/sys` when scanning home ✅
- Cross-filesystem links don't cause spanning ✅
- Symlink loops detected and handled ✅ (never followed); bind-mount cycles detected ✅ (G1 guard)
- Protected paths correctly identified ✅
- Unit tests with temp dirs cover edge cases ✅

> **Implemented:** `src/platform/mounts.*` (`/proc/self/mountinfo` parsing,
> statvfs capacity, pseudo/network detection), `src/core/protections.*`,
> `src/core/utils.*` (component-wise protection matching), and the traversal
> rules in `src/core/scanner.cpp`. Verified by `tests/core/test_mounts.cpp`
> and the `protected skip` case in `tests/core/test_scanner.cpp`.
>
> **G1 (bind-mount cycle guard):** the scanner keeps a recursion-stack set of
> `(dev, ino)` for active directories and skips any ancestor re-appearing via a
> bind mount (`SkipReason::LoopDetected`). Covered by the `scanner_userns`
> CTest target, which re-runs the scanner inside `unshare -rm` so a real
> bind-mount cycle is created (returns 77/skip where user namespaces are
> unavailable).

---

## Phase 3: Filesystem Scanner Implementation
**Goal:** Implement efficient scanner with progress, cancellation, hard-link dedup, and size calculation.  
**Duration:** 2-4 days  
**Status:** ✅ Complete

### Deliverables
- [x] Recursive directory scanner (thread-confined; GUI worker thread lands in Phase 8)
- [x] Progress reporting (files/dirs processed, bytes; ETA deferred — G3)
- [x] Cancellation support
- [x] Hard-link detection using (dev, ino)
- [x] Logical and allocated size calculation
- [x] Skipped paths tracking with reasons
- [x] Scan options/config

### Tasks
1. **Core scanner** (`src/core/scanner.cpp/h`)
   - [x] Thread-confined API: `scan()` + `cancel()` (start/pause handled by caller)
   - [x] Progress callback: (files, dirs, bytes, depth, current_path)
   - [x] Stream every file via item callback; retain bounded top-N in result
   - [x] Handle directories, regular files, symlinks separately

2. **Size calculation**
   - [x] Logical size: `file_size()` or `st_size`
   - [x] Allocated size: best effort via `st_blocks * 512` (or `stat` block size). Note confidence if unavailable
   - [x] Handle sparse files (logical >> allocated)
   - [x] For directories, compute recursive totals post-scan or during

3. **Deduplication & correctness**
   - [x] Build map: `key(dev, ino) -> count` to detect hard links
   - [x] If link count > 1, mark as hard-linked and avoid double-counting allocation for "recoverable" calculation
   - [x] Flag for hard-linked items
   - [x] CoW/Btrfs: document limitations in confidence

4. **Error handling**
   - [x] Collect skipped: `{path, reason}` in {PermissionDenied, NotAccessible, Loop, CrossFilesystem, Protected, IsSymlink, PseudoFilesystem, Other}
   - [x] Continue on ENOENT, EACCES gracefully
   - [x] Bound recorded skips (kMaxRecordedSkips) to avoid unbounded memory

### Acceptance Criteria
- Scans large directories without UI freeze (caller runs it off the UI thread) ✅
- Cancellation works mid-scan ✅
- Hard links don't double-count allocated space ✅
- Skipped paths listed with clear reasons ✅
- Progress updates reasonable ✅
- Unit tests: hard links, sparse files, symlinks, deep trees ✅

> **Implemented:** `src/core/scanner.{h,cpp}` — hard-link dedup via `(dev, ino)`,
> `st_blocks*512` allocated size, sparse detection, per-directory aggregates,
> bounded top-N retention, progress + cancellation, and skipped-path reasons.
> Verified by `tests/core/test_scanner.cpp` (aggregation, hard links, sparse,
> exclude, top-N, cancellation, protected skip).

---

## Phase 4: Device Discovery & Dashboard Data
**Goal:** List mounted filesystems/devices and compute usage stats for dashboard.  
**Duration:** 1 day  
**Status:** ✅ Complete

### Deliverables
- [x] Device discovery service
- [x] Mount list with sizes (total/used/available)
- [x] Device classification (local vs remote vs virtual)
- [x] Dashboard-ready data model

### Tasks
1. **Device enumeration** (`src/platform/devices.*`)
   - [x] Parse `/proc/mounts` / `/proc/self/mountinfo`
   - [x] For each mount, get capacity via `statvfs`
   - [x] Compute used = total - free (and available-to-user separately)
   - [ ] Distinguish physical devices vs partitions via sysfs/udev (G2 — deferred)
   - [x] Exclude pseudo-FS from device list shown to user

2. **Filtering**
   - [x] Filter out pseudo/network filesystems (`getRelevantMounts`)
   - [x] Hide proc/sys/tmpfs from selectable scan targets
   - [ ] Explicit removable-media (USB) classification via sysfs (G2 — deferred)

### Acceptance Criteria
- Lists all user-accessible local mounts
- Correct capacity numbers
- Pseudo-filesystems excluded from main list
- Handles missing/unreadable mounts

---

## Phase 5: Cleanup Recommendation Engine
**Goal:** Classify files/dirs as cleanup candidates with risk levels and justifications.  
**Priority:** **CRITICAL** (Safety)  
**Duration:** 1-2 days  
**Status:** ✅ Complete

### Deliverables
- [x] Heuristic rules for common caches
- [x] Risk classification logic
- [x] Confidence scoring
- [x] "Safe to delete?" policy separation (heuristic ≠ proof)
- [x] Test coverage for rules

### Tasks
1. **Risk taxonomy**
   - [x] `Safe`: verified disposable regenerable caches with strong signals
   - [x] `Low`: likely disposable, needs confirmation
   - [x] `Medium`: ambiguous (e.g. old downloads, build dirs) - requires careful review
   - [x] `High`: user data or unclear ownership
   - [x] `SystemProtected`: matches protected paths - never auto-select

2. **Heuristics** (`src/core/recommendations.*`)
   - [x] Thumbnail caches: `~/.cache/thumbnails/`, `~/.thumbnails/` (age + regenerable)
   - [x] Browser caches (profile-specific) - mark as low/med
   - [x] Package manager caches: `~/.cache/pacman/pkg/`? or system - be conservative; system package caches often need package manager
   - [x] Temp dirs: `/tmp`, `~/.cache/tmp` (if old, user-owned)
   - [x] Build artifacts: `target/`, `build/`, `CMakeCache.txt`, `*.o` - user projects
   - [x] APT cache: `/var/cache/apt/archives` - **system**: high risk, must warn; better delegated
   - [ ] Explicit Flatpak/Snap cache detection (G5 — deferred; generic `~/.cache` rule partially covers)
   - [x] Downloads: never assume "old" = safe (user decision)

3. **Safety rules (hard constraints)**
   - [x] If path under protected system dirs → `SystemProtected`, cannot delete
   - [x] If path is root-owned and not in explicit safe user caches → at least `Medium` or `High`
   - [x] Filename alone insufficient (explicitly enforce this)
   - [x] Symlinks: handle separately, don't follow
   - [x] If hard-linked and other references exist, note impact

4. **Confidence & estimates**
   - [x] Track confidence in "recoverable space" (allocation accuracy, CoW, hard links)
   - [ ] Surface estimates/labels in the UI (Phase 9 — UI not built yet)
   - [x] Don't claim full physical free if shared

### Acceptance Criteria
- Protected paths never classified as Safe ✅
- Name-only heuristics rejected ✅
- Each candidate has reason + risk + confidence ✅
- Conservative by default (prefer Medium/High over Safe) ✅
- Unit tests cover key cases ✅

> **Implemented:** `src/core/recommendations.{h,cpp}` — `RecommendationEngine`
> with conservative heuristics keyed on full path patterns (thumbnails, browser,
> package, temp, downloads, logs, build artifacts, generic app caches, system
> caches). Hard constraints: protected → `SystemProtected` (0 recoverable);
> non-user-owned → at least `High`; build artifacts → never below `Medium`;
> hard-linked files → 0 conservative recoverable. `permanentDeleteAllowed` is
> gated on opt-in + user ownership + regenerable category + age. `recommend()`
> deduplicates nested directories and excludes files already covered. Verified
> by `tests/core/test_recommendations.cpp` (15 cases incl. a real scan→recommend
> end-to-end).

> **Not yet covered:** explicit Flatpak/Snap cache rules (G5).

---

## Phase 6: Application Storage Analysis (Optional but Planned)
**Goal:** Estimate app footprints via package managers; delegate removals to PMs.  
**Duration:** 1-2 days  
**Status:** Pending

### Deliverables
- [ ] dpkg/APT integration (query only)
- [ ] pacman integration (query only)
- [ ] RPM/DNF integration (query only) - optional first
- [ ] Flatpak/Snap integration (query only) - optional first
- [ ] Clear "shared" warnings

### Tasks
1. **Query-only approach**
   - [ ] Parse `dpkg-query -S` or `dpkg -L <pkg>` carefully? Or use `apt-cache` size info
   - [ ] For pacman: `pacman -Qi`, `pacman -Ql`
   - [ ] Never execute `apt remove`, `pacman -R` from app - show instructions to user
   - [ ] Label: "Estimated (may include shared libs)" 
   - [ ] Avoid summing shared deps multiple times

2. **UI integration**
   - [ ] App list with name, version, size estimate, source (APT/Flatpak)
   - [ ] "View details" but no delete action
   - [ ] Link to open PM GUI or show command

### Acceptance Criteria
- Read-only, no destructive PM calls
- Clear disclaimers about shared dependencies
- Works on common distros (Ubuntu/Debian, Arch first)

---

## Phase 7: Core Logic Integration & Utilities
**Goal:** Add logging, config, error handling, audit trail.  
**Duration:** 0.5-1 day  
**Status:** ✅ Complete

### Deliverables
- [x] Config system (INI-style, dependency-free; XDG-aware)
- [x] Audit logger (append-only JSON Lines)
- [x] Error reporting helpers (`describeErrno`, audit `errors[]`)
- [x] Path utilities (normalize, `isPathUnder`, symlink-safe `canonicalizeNoFollow`)
- [x] String/format helpers for sizes (`formatBytes`, `formatDuration`, `abbreviatePath`)

### Tasks
1. **Configuration** (`src/utils/config.*`)
   - [x] Default protected paths, exclude patterns
   - [x] Tunables (age thresholds, permanent-delete opt-in) feeding the engine
   - [x] Load/save to `~/.config/driverse/` or XDG
   - [ ] UI prefs (sort, columns, confirm thresholds) — Phase 8

2. **Audit logging** (`src/utils/audit.*`)
   - [x] Append-only: timestamp, user, action, paths[], sizes, risk, result
   - [x] Write to `~/.local/share/driverse/audit.jsonl` (XDG)
   - [x] Include errors[]
   - [x] Human-readable + machine-parseable (JSONL)
   - [ ] scan id/duration — folded into Phase 10 record

3. **Logging framework** (`src/utils/logger.*`)
   - [x] Simple logger (console + optional file sink)
   - [x] Log levels: Debug, Info, Warn, Error (with min-level filtering)
   - [x] Path abbreviation helper to avoid leaking long/sensitive paths

4. **Integration bridge** (`src/utils/integration.*`)
   - [x] `Config` → `RecommendationOptions` / `ScanOptions`
   - [x] Apply configured protected paths to `Protections` (idempotent)

### Acceptance Criteria
- Config persists across runs ✅
- Audit log records operations as append-only JSONL ✅
- Logs don't leak excessively ✅ (path abbreviation helper available)
- Config-driven protected paths actually take effect ✅

> **Implemented:** `src/utils/{config,audit,logger,integration}.{h,cpp}` — all pure
> std, no Qt coupling, fully unit-tested (config, audit, logger, integration).
> 
> **Fixed (real bug):** `Protections::isProtected` previously ignored its own
> registry (`m_protectedPaths`) and only consulted the built-in defaults, so any
> path added via `addProtectedPath` (e.g. from config) had **no effect**. It now
> checks both defaults and registered paths, component-wise without following
> symlinks. Covered by `tests/utils/test_integration.cpp`.

---

## Phase 8: Qt GUI - Dashboard & Device Selection
**Goal:** Build main UI with device list, storage overview, and scan controls.  
**Duration:** 1-2 days  
**Status:** ✅ Complete

### Deliverables
- [x] Main window with layout
- [x] Device list/table (mount, fs, total/used/free, %)
- [x] Storage utilization chart (custom QPainter donut — Q1 resolved)
- [x] Scan controls (Select dir, Scan, Cancel)
- [x] Status bar (progress, stats)
- [x] Menu/actions

### Tasks
1. **Main window** (`src/gui/mainwindow.*`)
   - [x] QMainWindow with splitter
   - [x] Left: device/filesystem panel
   - [x] Right: dashboard area
   - [x] Bottom: status/progress

2. **Device model/view**
   - [x] `DeviceTableModel` inheriting `QAbstractTableModel`
   - [x] Show: Device/Mount, Type, Total, Used, Free, Usage%
   - [x] Selection enables "Scan Selected"
   - [x] Visual indicators for virtual/remote

3. **Dashboard widgets**
   - [x] Donut/pie chart for usage (dependency-free custom widget)
   - [x] Summary cards: Total, Used, Free, Est. Recoverable
   - [x] "Largest dirs preview" (post-scan)
   - [x] "Scan" button with path selection

4. **Threading**
   - [x] Use `QtConcurrent::run` for scanning (`ScanWorker`)
   - [x] Emit signals for progress/results (queued to GUI thread)
   - [x] Keep UI responsive, handle cancellation

### Acceptance Criteria
- Device list populates on startup ✅
- Can select mount point and start scan ✅
- Progress shown during scan ✅
- Cancel button works ✅
- UI remains responsive ✅ (smoke-tested headless under `offscreen`)

> **Implemented:** `src/gui/{mainwindow,deviceTableModel,storageChart,summaryCard,scanworker}.{h,cpp}`
> 
> - **Q1 resolved:** the donut is a custom `QPainter` `StorageChart` widget —
>   no `qt6-charts` dependency, keeps the build reproducible.
> - `DeviceTableModel` (QAbstractTableModel): shows mount/device/type/total/
>   used/free/usage%, greys virtual/remote rows, highlights high usage (>90%).
> - `ScanWorker` runs the scanner via `QtConcurrent::run` on a background
>   thread, emits queued progress/completed/failed signals, and supports
>   cooperative cancel. It also snapshots the top-level directories and runs
>   the recommendation engine to produce a conservative recoverable estimate.
> - `MainWindow`: split-view (device table | dashboard), summary cards, donut,
>   largest-dirs preview, scan/cancel controls, status + progress bar, and
>   File/Scan/Help menus. Loads config, applies custom protected paths.
> - **Fixed (latent UB):** `DeviceInfo` had no default member initializers, so
>   partially-populated entries had uninitialized bools (`isPseudoFs`/
>   `isNetworkFs`). Added defaults in `types.h`.

> **Delivered in Phase 9:** full results view (files/dirs/recommendations tables),
> sorting/filtering, risk colours, per-item actions, details panel.
> 
> **Known gap carried forward:** G3 (no total-scan estimate → progress bar uses
> an indeterminate busy state).

---

## Phase 9: Results View - Files, Dirs, & Recommendations
**Goal:** Display scan results with sorting, filtering, and risk classification.  
**Duration:** 1-2 days  
**Status:** ✅ Complete

### Deliverables
- [x] Results tabs: "Largest Files", "Largest Directories", "Recommendations"
- [x] Sortable/filterable table
- [x] Risk badges/colors
- [x] Path actions (Open containing dir, Copy path)
- [x] Details panel

### Tasks
1. **Results table model** (`src/gui/scanmodel.*`)
   - [x] Virtual model for large datasets (visible-index list, row cap)
   - [x] Columns: Name, Path, Type, Logical Size, Allocated Size, Modified, Risk
   - [x] Sort by any column
   - [x] Role-based formatting (sizes, dates)

2. **Filtering**
   - [x] Filter by risk level (Safe/Low/Med/High/Protected)
   - [x] Filter by path substring
   - [x] Filter by min size
   - [x] "Show only recommended for cleanup"

3. **Recommendations tab**
   - [x] Group by risk (sorted by risk; per-group select)
   - [x] Show justification for each
   - [x] Show estimated recoverable
   - [x] Selection checkboxes (multi-select)
   - [x] "Select all in group" with caution

4. **Interactions**
   - [x] Double-click to open in file manager (`QDesktopServices` / xdg-open parent)
   - [x] Copy path to clipboard (context menu)
   - [x] Show tooltips with details

### Acceptance Criteria
- Large results load smoothly (virtual model) ✅
- Sorting/filtering fast enough ✅ (debounced filters, index-based rebuild)
- Risk clearly visible ✅ (colour-coded Risk column)
- Path actions work ✅ (context menu + double-click)
- Can inspect items before cleanup ✅ (details panel)

> **Implemented:** `src/gui/{scandata.h,scanmodel.*}` + `ScanWorker` result
> plumbing + `MainWindow` results area.
> 
> - `ScanData` is an immutable snapshot (`shared_ptr`) of the full `ScanResult`,
>   the recommendation list, a path→risk index, and the total recoverable.
>   `ScanWorker` builds it on the worker thread and hands it to the models.
> - `ResultsTableModel` is a filterable/sortable `QAbstractTableModel` with three
>   modes (Files, Directories, Recommendations). Rows are addressed through a
>   visible-index list so filtering/sorting never copies items; a 200k-row cap
>   keeps a pathological scan from stalling the view.
> - Filters: path substring, minimum size, risk mask, "recommended only"; applied
>   to all three tabs and debounced (200 ms) from the UI.
> - Recommendations tab: checkboxes per row with "Select group", "Select all
>   shown" and "Clear selection"; a live "N selected — X recoverable" summary.
> - Details panel shows full metadata per row; context menu offers Open
>   containing folder / Copy path / Copy details; double-click opens the parent.
> 
> **Test coverage:** `tests/gui/test_scanmodel.cpp` (filters, sorting, risk mask,
> checkbox selection) and `tests/gui/test_scanworker.cpp` (real background scan →
> `ScanData`). 13/13 suites pass, zero warnings.

> **Deferred to Phase 10:** the cleanup workflow itself (dry-run, confirmation,
> execution/trash, audit logging of actions). The Recommendations tab currently
> only *selects* candidates; nothing is deleted.

---

## Phase 10: Cleanup Workflow - Dry-Run, Confirmation, & Execution
**Goal:** Implement safe cleanup with mandatory dry-run and confirmations.  
**Priority:** **CRITICAL** (Safety)  
**Duration:** 1-2 days  
**Status:** ✅ Complete (2026-10-16)

### Deliverables
- [x] Dry-run preview dialog
- [x] Risk-based confirmation system
- [x] Cleanup executor (non-destructive simulation first)
- [x] Trash integration (user files)
- [x] Audit logging of all actions
- [x] Error handling & partial results

### Tasks
1. **Dry-run enforcement**
   - [x] All operations start as dry-run
   - [x] Preview shows: count, total est. freed, risk breakdown, top items
   - [x] Must confirm each risk group or overall
   - [x] Compare against protected paths (re-validate)

2. **Confirmation UI**
   - [x] For `Safe`/`Low`: checkbox confirmation
   - [x] For `Medium`: stronger confirmation + "I understand" checkbox
   - [x] For `High`: require explicit checkbox + list review
   - [x] For `SystemProtected`: never allow
   - [x] Option to "Review selected" before proceeding
   - [x] Type-to-confirm for High risk if root-owned paths selected

3. **Cleanup execution** (`src/platform/trash.*`, `src/core/cleanup.*`)
   - [x] Prefer XDG Trash: move user-owned files to trash (`gio trash`, `trash-put`, or manual XDG spec)
   - [x] For regenerable caches in safe locations, allow permanent delete ONLY if explicitly opted-in AND classified Safe
   - [x] Never delete if path became protected between preview and exec (recheck)
   - [x] Never follow symlinks
   - [x] Handle directories: remove empty dirs only if safe and user-approved
   - [x] Atomic-ish batch: process in order, log each
   - [x] Track success/failure/skipped

4. **TOCTOU safety**
   - [x] Snapshot (path, dev, ino, size) at preview time
   - [x] On delete: verify still exists, same inode/dev (where possible), size not drastically changed?
   - [x] If drift detected: warn, skip or abort batch
   - [x] Abort entire batch on critical mismatch

5. **Audit trail**
   - [x] Log: `{"ts":..., "action":"cleanup", "mode":"dryrun|apply", "user":..., "items":[...], "total_freed":..., "result":"ok|partial|failed", "errors":[]}`
   - [x] Write to audit log before and after

### Acceptance Criteria
- Dry-run is mandatory (no direct delete without preview)
- Protected paths cannot be deleted
- Trash used for user personal files by default
- Permanent delete requires explicit opt-in
- TOCTOU check prevents deleting wrong file on mismatch
- Audit log complete
- Partial failures reported clearly

### Delivered (2026-10-16)
- **`src/platform/trash.{h,cpp}`** — dependency-free XDG Trash implementation:
  `files/` + `info/` layout, percent-encoded `Path=` record, `DeletionDate`,
  collision-safe names (`name.1`, `name.2`, …), rename-first with a
  copy+remove fallback across filesystems, and symlinks moved as links (targets
  never followed). The `.trashinfo` record is written *before* the move, so a
  partial failure cannot leave an untracked file in the trash. Deviation from
  the spec: the per-topdir `.Trash-<uid>` variant is not implemented; cross-device
  moves are copied into the home trash instead (still recoverable).
- **`src/core/cleanup.{h,cpp}`** — `buildCleanupPlan()` snapshots each candidate
  (dev, ino, logical size, type, owner via `lstat`) and assigns `MoveToTrash`,
  `PermanentDelete`, or `Skip` (+reason) with risk/byte aggregation.
  Hard guards: `SystemProtected` and protected paths rejected; non-user-owned
  items refused (needs root); "/", home, and any ancestor of home refused even
  if a bad candidate reaches the plan. `executeCleanup()` re-validates protected
  paths, re-checks `(dev, ino, type)` identity (TOCTOU) and — by default —
  aborts the whole batch on a mismatch (`abortOnDrift = false` → skip the item);
  size drift is reported as a note. Reports track `MovedToTrash` (recoverable,
  bytes still on disk) separately from `Deleted` (actually reclaimed), plus
  skipped/failed counts, abort reason, and duration. Dry-run mode touches
  nothing and returns the same report shape.
- **`src/utils/cleanupaudit.{h,cpp}`** — maps plan/report onto `AuditEntry`:
  "begin" record before an apply (`result=planned`), preview record for dry runs
  (`action=preview`), and a result record after execution with per-item
  `trashed|deleted|skipped|failed|dry-run` and aggregated errors.
- **`src/gui/cleanupdialog.{h,cpp}`** — "Clean up selected…" in the
  Recommendations tab opens a preview: plan summary (count, estimated
  recoverable, per-risk breakdown, planned trash/delete split), a review table
  (riskiest/largest first, skipped items shown with reasons), warning banner,
  then the confirmation gates: acknowledgement checkbox (always), "I have
  reviewed the high risk items" (High only), permanent-delete opt-in (only when
  eligible Safe+regenerable items exist) which forces typing `DELETE` (also
  required for High risk). **Apply is refused unless the audit "before" record
  is written successfully** — no unaudited destructive action. A modal
  Yes/No warning is the final gate. Buttons: Dry run / Apply / Close.
- **Selection wiring** — `MainWindow` enables the button only with ≥1 checked
  candidate and not while scanning; after a successful apply it rescans to
  refresh results.
- **Tests (17 suites total)** — `test_trash` (move file/dir/symlink, collision
  naming, encoding, missing path), `test_cleanup` (plan decisions, opt-in
  gating, ownership rule, dangerous roots, dry run touches nothing, trash apply,
  TOCTOU abort/skip, protected re-check, status strings), `test_cleanupaudit`
  (begin/preview/apply/aborted entries, JSON round-trip), `test_cleanupdialog`
  (pure gate functions + dialog gating, end-to-end apply with redirected
  `XDG_DATA_HOME` asserting the trash contents and the two audit records, and
  the audit-unwritable refusal).
- **Notes for later:** execution is synchronous (no progress dialog/cancel
  mid-batch); directories trash/move as a whole (no per-file empty-dir sweep);
  `remove_all` for opted-in directory deletion does not follow symlinks.

---

## Phase 11: Polish, Error Handling & Edge Cases
**Goal:** Improve UX, robustness, and safety for real-world use.  
**Duration:** 1 day  
**Status:** ✅ Complete (2026-10-16)

### Deliverables
- [x] Improved error messages
- [x] "Skipped paths" dialog with reasons
- [x] Empty state messages
- [x] Tooltips and help text
- [x] Keyboard shortcuts
- [x] About dialog

### Tasks
1. **UX polish**
   - [x] Size formatting (B, KiB, MiB, GiB, TiB) with locale-aware presentation
   - [x] Date formatting (locale short format in tables/details)
   - [x] Loading spinners/progress bars (indeterminate scan progress; busy cursor during cleanup)
   - [x] Risk color coding (consistent palette, single source `gui/formatting.h`)
   - [x] "What's this?" tooltips explaining estimates

2. **Robustness**
   - [x] Handle unmounted paths (target validation + friendly errors before scanning)
   - [x] Handle permission denied everywhere (pre-scan check + scanner skip list + dialog)
   - [x] Handle disk full during cleanup (ENOSPC)
   - [x] Handle busy files (EBUSY)
   - [x] Handle read-only FS (EROFS)
   - [x] Network mounts: warn or be cautious

3. **Safety review**
   - [x] Re-validate all protected checks
   - [x] Ensure no root traversal via `..` issues (lexical normalization + tests)
   - [x] Test with fake protected paths
   - [x] Verify audit log integrity

### Acceptance Criteria
- Graceful handling of common errors ✅
- Clear user feedback ✅
- No crashes on edge cases ✅
- Safety checks comprehensive ✅

### Delivered (2026-10-16)
- **Error handling** — `utils::fsErrorHint()` maps ENOSPC / EBUSY / EROFS /
  EACCES+EPERM / EXDEV / ENOTEMPTY / ENOENT / EIO to actionable advice, appended
  to cleanup delete failures (`core/cleanup.cpp`) and trash copy/rename/remove
  failures (`platform/trash.cpp`). `MainWindow::startScan` pre-validates the
  target (exists / directory / readable) with specific dialogs, so an unmounted
  device or permission error never reaches the scanner as a cryptic failure.
  Network filesystems (mountinfo classification) raise a confirmation warning
  before scanning.
- **Skipped paths** — `gui/skippedpathsdialog.{h,cpp}`: read-only table
  (path + reason, per-reason summary, cap note) for everything the scanner did
  not descend into; wired to Scan ▸ "Skipped paths…", enabled only when the last
  scan recorded skips.
- **Empty states** — results area shows a `QStackedWidget` placeholder when the
  active tab has no rows: "no scan yet", "no recommendations" (conservative by
  design), or "no items match the current filters".
- **Formatting & palette** — `gui/formatting.h` centralizes locale-aware
  `bytes()` (IEC), locale short dates, risk names and the five-level risk colour
  palette; `scanmodel`, `cleanupdialog`, `mainwindow`, `deviceTableModel` all
  use it (cleanup dialog no longer has hardcoded warning colours).
- **Shortcuts & help** — Ctrl+O select folder, F5 scan, Esc cancel (existing),
  plus Ctrl+F focuses the path filter and Ctrl+1/2/3 switch tabs. Tooltips on
  cards, filters, risk checkboxes, buttons, tabs and the chart; About dialog now
  shows the Qt version and the audit-log path.
- **Safety review** — verified the cleanup path re-canonicalizes + re-checks
  protection at plan *and* execution time; added `testProtectedRegistryAtPlanTime`
  (runtime-registered protected paths and `..`-containing candidates are skipped
  before any filesystem action) and `fsErrorHint`/formatting tests. Audit
  before/after records were already covered by `test_cleanupaudit`.
- **Known limitations (unchanged, tracked):** G3 (no total/ETA progress
  estimate), G4 (sensitive-path heuristics minimal), cleanup execution remains
  synchronous (no cancel mid-batch).

---

## Phase 12: Testing, Documentation & Packaging
**Goal:** Comprehensive testing and user documentation.  
**Duration:** 1-2 days  
**Status:** Pending

### Deliverables
- [ ] Unit tests (core logic)
- [ ] Integration tests (safety scenarios)
- [ ] Manual test checklist
- [ ] README.md (updated)
- [ ] USER_GUIDE.md
- [ ] CONTRIBUTING.md
- [ ] Basic packaging (AppImage or .desktop + binary)

### Tasks
1. **Unit tests** (Catch2 or Qt Test)
   - [ ] Mount parsing
   - [ ] Path protection
   - [ ] Risk classification
   - [ ] Hard link dedup
   - [ ] Size calculations
   - [ ] Recommendation rules

2. **Integration/safety tests**
   - [ ] Scan temp tree with hard links, symlinks, sparse files
   - [ ] Dry-run never deletes
   - [ ] Protected paths blocked
   - [ ] Cross-filesystem handling
   - [ ] TOCTOU detection
   - [ ] Trash vs permanent delete

3. **Documentation**
   - [ ] Update README.md with build/run instructions
   - [ ] Add screenshots (future)
   - [ ] Safety notes prominently
   - [ ] Explain "estimates", logical vs allocated
   - [ ] Warn about system dirs

4. **Packaging**
   - [ ] `.desktop` file
   - [ ] App icon placeholder
   - [ ] Build instructions for distros
   - [ ] Optional: CPack config

### Acceptance Criteria
- All unit tests pass
- Safety tests pass
- Build instructions clear
- Documentation accurate

---

## Implementation Strategy

### Development Order
We must follow **safety-first** order. Critical safety phases (2, 5, 10) come before GUI polish.

**Recommended sequence:** Phase 0 → 1 → 2 → 3 → 4 → 5 → 7 → 8 → 9 → 10 → 11 → 12. Phase 6 (apps) can be done in parallel later or after core works.

### Branching Strategy
- `main`: stable releases
- `dev`: integration
- Feature branches: `feat/phase-N-*`

### Testing Strategy
- **TDD where appropriate** for core safety logic (protections, recommendations)
- **Manual testing** on real system with extreme caution (never run on production without dry-run)
- Use isolated temp directories for automated tests

### Definition of "Done" (per phase)
- [ ] Code compiles with `-Wall -Wextra -Wpedantic` (warnings addressed)
- [ ] Unit/integration tests pass
- [ ] Basic manual verification done
- [ ] No destructive operations in code path without checks
- [ ] Audit/logging added where relevant
- [ ] Committed with clear message

---

## Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Accidental deletion of system files | Low (with guards) | Critical | Multiple layers: denylist, re-check, dry-run mandatory, trash-first |
| Hard link miscounting | Med | Med | Track (dev,ino), count links, test fixtures |
| TOCTOU between preview/apply | Med | High | Snapshot + verify inode/dev/size, abort on drift |
| Scanning huge FS blocks UI | Med | Low | Background threads, cancel, virtual model |
| Cross-filesystem traversal | Low | Med | Enforce st_dev boundary |
| CoW allocated size inaccurate | Med | Low | Label confidence, show estimates clearly |

---

## Open Questions & Known Gaps

| # | Item | Notes / Decision needed |
|---|---|---|
| Q1 | **Qt Charts dependency** | ✅ **Resolved (Phase 8).** `qt6-charts` is not installed and cannot be (needs sudo password), so the dashboard donut is a **custom `QPainter` `StorageChart` widget** — no extra dependency, reproducible build, and the existing `DeviceInfo` capacity data is enough. If Qt Charts is added later, only `StorageChart` needs swapping. |
| Q2 | **Bind-mount recursion** | ✅ **Resolved.** `st_dev` boundary did not stop a bind mount of an ancestor (same device) → potential infinite recursion. Fixed with a recursion-stack `(dev, ino)` set that skips an ancestor re-appearing via bind mount (G1). Covered by the `scanner_userns` CTest (runs under `unshare -rm`). |
| Q3 | **CoW/reflink accounting** | Btrfs/ZFS allocated sizes are lowered in confidence but still summed. Reflinked extents (e.g. `cp --reflink`) are not deduplicated like hard links. Acceptable for v1 if labelled; revisit later. |
| Q4 | **Test framework** | Tests currently use plain `assert` + CTest. Plan mentions Catch2/Qt Test. Decision: keep lightweight for now; adopt a framework only if fixture complexity grows. |

### Known gaps (tracked)

Resolved:
- **G1 (safety):** ✅ Recursion-stack cycle guard for bind-mount loops implemented and tested (see Q2). `SkipReason::LoopDetected` now covers filesystem cycles.
- **G6 (safety):** ✅ `Protections::isProtected` now honors runtime-registered paths (it previously ignored its own `m_protectedPaths` registry, so custom config paths had no effect). Fixed and tested in Phase 7.
- **G7 (correctness):** ✅ `DeviceInfo` had no default member initializers, so partially-populated entries (e.g. from the GUI) carried uninitialized bools (`isPseudoFs`/`isNetworkFs`) — a latent UB that surfaced in the device-table test. Added defaults in `types.h`.

Open:
- **G2:** physical-device vs partition classification and removable-media detection (Phase 4) not implemented; only pseudo/network filtering exists.
- **G3:** progress "total estimate"/ETA not implemented; scan reports processed counts only.
- **G4:** sensitive-path warnings (`Protections::isSensitive`) are minimal heuristics.
- **G5:** explicit Flatpak/Snap cache detection (Phase 5) not implemented; the generic `~/.cache` app-cache rule partially covers them.

---

## Progress Log

- **2026-10-15** — `34dc2aa` initial commit (requirements PDF, stub README).
- **2026-10-15** — `f0fb93b` Phases 0–4 data layer: build system, types, utils,
  protections, mount detection, hard-link-aware scanner; 4 CTest suites passing,
  zero warnings.
- **2026-10-15** — `19a539f` Plan review: corrected structure tree, added Open
  Questions (Qt Charts) and Known Gaps, marked completed-phase checkboxes.
- **2026-10-15** — G1 bind-mount cycle guard: recursion-stack `(dev, ino)` set in
  `src/core/scanner.cpp`; `SkipReason::LoopDetected` for cycles; new
  `scanner_userns` CTest runs the scanner under `unshare -rm` to exercise a real
  bind-mount cycle. 5/5 tests pass.
- **2026-10-15** — Phase 5 recommendation engine: `src/core/recommendations.*`
  with conservative heuristics, ownership/age/hard-link safety rules, and
  `recommend()` dedup; scanner now records `ownerUid` + `mtime`; 15 new tests
  incl. end-to-end scan→recommend. 6/6 tests pass.
- **2026-10-15** — Phase 7 core integration & utilities: new `src/utils/` module
  (INI `config`, JSONL `audit` log, leveled `logger`, `integration` bridge),
  plus `describeErrno`/`abbreviatePath`/`isPathUnder`/`canonicalizeNoFollow`
  helpers. Fixed safety bug G6 (`Protections::isProtected` ignoring registered
  paths). 4 new test suites; 10/10 tests pass.
- **2026-10-15** — Phase 8 Qt GUI dashboard & device selection: new `src/gui/`
  module (`mainwindow`, `deviceTableModel`, `storageChart`, `summaryCard`,
  `scanworker`). Custom `QPainter` donut resolves Q1 (no Qt Charts dependency);
  scans run on a background thread via `QtConcurrent` with queued
  progress/completed/failed signals and cooperative cancel. Added a device-table
  GUI test and fixed latent UB G7 (`DeviceInfo` missing default initializers).
  11/11 tests pass, zero warnings, app smoke-tested headless.
- **2026-10-15** — Phase 9 results view: `ScanData` snapshot + `ResultsTableModel`
  (filterable/sortable, three modes: largest files, largest directories,
  checkable recommendations) + a `MainWindow` results area with filter bar,
  details panel, risk colours, and path actions (open containing folder / copy
  path via context menu, double-click). `ScanWorker` now hands the full result to
  the views. New tests `test_scanmodel` and `test_scanworker` (real background
  scan → snapshot). 13/13 tests pass, zero warnings, app smoke-tested headless.
- **2026-10-16** — Phase 10 cleanup workflow (safety-critical):
  `src/platform/trash.*` (XDG Trash, recoverable, symlink-safe),
  `src/core/cleanup.*` (plan + TOCTOU-verified executor, trash-first, opt-in
  permanent delete for Safe regenerable caches, repeated protected-path checks,
  home/root hard guards, partial-failure reporting), `src/utils/cleanupaudit.*`
  (before/after audit records), and `src/gui/cleanupdialog.*` ("Clean up
  selected…" preview with risk breakdown, review table, acknowledgement /
  high-risk / type-to-confirm gates, dry run + apply). Apply refuses to run when
  the audit log cannot be written. The app is no longer read-only: it can move
  user cache candidates to the trash after explicit confirmation. New tests
  `test_trash`, `test_cleanup`, `test_cleanupaudit`, `test_cleanupdialog`
  (end-to-end apply with redirected `XDG_DATA_HOME`). 17/17 tests pass, zero
  warnings, app smoke-tested headless.
- **2026-10-16** — Phase 11 polish, error handling & edge cases:
  `utils::fsErrorHint` (ENOSPC/EBUSY/EROFS/EACCES/EXDEV/ENOENT/EIO) wired into
  cleanup + trash failures; scan-target validation and a network-filesystem
  warning in `MainWindow`; `gui/skippedpathsdialog.*` (Scan ▸ "Skipped paths…");
  empty-state stack in the results area; `gui/formatting.h` (locale-aware sizes
  and dates, shared risk palette) adopted by all models/dialogs; shortcuts
  Ctrl+F / Ctrl+1..3; tooltips across cards, filters, tabs and the chart; About
  dialog with Qt version + audit-log path. Safety review confirmed plan- and
  execution-time protection checks, with a new `..`/registry plan-time test.
  New tests `test_skippedpathsdialog`, `test_formatting`, extended `test_utils`,
  `test_cleanup`, `test_scanworker`. 19/19 tests pass, zero warnings, app
  smoke-tested headless.

---

## Next Steps

Phases 0–5 and 7–11 are done and tested. The app can scan a filesystem or folder
on a background thread, present the largest files, largest directories, and
conservative risk-classified recommendations with filtering, sorting, a details
panel, empty states, and path actions, and — after an explicit dry-run-style
review — move selected candidates to the XDG Trash (or permanently delete
opted-in Safe caches), recording every action in the audit log. Failures carry
actionable hints (disk full, busy, read-only, ...), skipped paths are explained
in a dialog, and risky targets (network mounts, unmounted paths) are surfaced
before the scan starts.

Next is **Phase 12: Testing, Documentation & Packaging** — README/USER_GUIDE,
safety notes, a manual test checklist, `.desktop` file + icon placeholder, and
optional CPack packaging. Phase 6 (application storage analysis via package
managers, query-only) remains optional and can slot in after packaging.

Q1 is resolved: the donut is a custom `QPainter` widget (no `qt6-charts`
dependency), so the build stays reproducible. If you later install
`qt6-charts`, only `StorageChart` needs swapping.