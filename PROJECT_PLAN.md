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

_Last updated: 2026-10-15_

| Phase | Title | Status |
|---|---|---|
| 0 | Foundation & Environment Setup | ✅ Complete |
| 1 | Core Data Models & Types | ✅ Complete |
| 2 | Filesystem Safety & Mount Detection | ✅ Complete |
| 3 | Filesystem Scanner Implementation | ✅ Complete |
| 4 | Device Discovery & Dashboard Data | ✅ Complete (data layer) |
| 5 | Cleanup Recommendation Engine | ⏳ In progress (next) |
| 6 | Application Storage Analysis | ⬜ Pending |
| 7 | Core Logic Integration & Utilities | ⬜ Pending |
| 8 | Qt GUI - Dashboard & Device Selection | ⬜ Pending |
| 9 | Results View - Files, Dirs, & Recommendations | ⬜ Pending |
| 10 | Cleanup Workflow (Dry-Run, Confirm, Execute) | ⬜ Pending |
| 11 | Polish, Error Handling & Edge Cases | ⬜ Pending |
| 12 | Testing, Documentation & Packaging | ⬜ Pending |

**What exists today:** buildable C++20/Qt6/CMake project; core static library
(`driverse_core`) with types, utilities, protections, mount detection, and a
hard-link-aware filesystem scanner; CTest suite (types, utils, mounts, scanner)
all passing with zero compiler warnings.

## Architecture Goals

- **Separation of concerns:** Core logic (scanner, analyzer) independent from GUI
- **Thread-safe:** Background scanning with progress/cancellation
- **Safety-first:** Dry-run by default, risk classification, audit logging
- **Cross-platform awareness:** Linux-specific where needed (mounts, package managers)
- **Testable:** Unit tests for core logic, integration tests for safety

## Project Structure

```
driverse/
├── CMakeLists.txt
├── src/
│   ├── core/           # Filesystem scanning, analysis, recommendations
│   ├── gui/            # Qt UI, models, views
│   ├── platform/       # Linux-specific: mounts, package managers, trash
│   ├── utils/          # Logging, config, helpers
│   └── main.cpp
├── tests/              # Unit & integration tests
├── data/               # Resources, icons
├── docs/               # Additional docs
└── scripts/           # Build/test scripts
```

## Phase 0: Foundation & Environment Setup
**Goal:** Set up build system, project structure, and development environment.  
**Duration:** ~1-2 hours  
**Status:** ✅ Complete

### Deliverables
- [ ] CMakeLists.txt with Qt6 dependencies configured
- [ ] Basic project directory structure
- [ ] `.gitignore` appropriate for C++/Qt/CMake
- [ ] Basic main.cpp with empty Qt app
- [ ] Build compiles successfully

### Tasks
1. **Create project skeleton**
   - [ ] Set up src/core, src/gui, src/platform, src/utils, tests
   - [ ] Add initial CMakeLists.txt

2. **Configure dependencies**
   - [ ] Find Qt6 (Core, Widgets, Charts, Concurrent)
   - [ ] Configure compiler flags: `-Wall -Wextra -Wpedantic`
   - [ ] Set C++20 standard

3. **Basic app bootstrap**
   - [ ] Create minimal Qt application
   - [ ] Verify build with `cmake` + `make/ninja`
   - [ ] Add CI placeholder or local build script

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
- [ ] `FileItem` struct (path, size, inode, dev, mtime, type, flags)
- [ ] `DeviceInfo` struct (mount point, device, fs type, total/used/free)
- [ ] `ScanResult` container
- [ ] `RiskLevel` enum and `CleanupCandidate` struct
- [ ] Unit tests for data models

### Tasks
1. **File metadata model**
   - [ ] Store logical size, allocated size
   - [ ] Store (dev, ino) for hard-link deduplication
   - [ ] Flags: isSymlink, isDir, isHardLinked, isProtected, isSparse
   - [ ] Category and risk level

2. **Device detection types**
   - [ ] Mount info structure
   - [ ] Capacity calculation helpers

3. **Recommendation types**
   - [ ] Risk taxonomy: Safe, Low, Medium, High, SystemProtected
   - [ ] Confidence level (High/Med/Low) for estimates
   - [ ] Justification/reason for each recommendation

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
- [ ] Mount point discovery (parse `/proc/mounts` and/or `/etc/mtab`)
- [ ] Filesystem boundary detection (device ID tracking)
- [ ] Protected paths system (denylist + detection)
- [ ] Pseudo-filesystem exclusion (proc, sys, dev, tmpfs, fuse, overlay)
- [ ] Symlink handling (no-follow by default, loop detection)
- [ ] One-filesystem traversal

### Tasks
1. **Mount detection** (`src/platform/mounts.*`)
   - [ ] Parse mount info from `/proc/self/mountinfo` or `/proc/mounts`
   - [ ] Get device major/minor, fs type, mount options
   - [ ] Classify: local, network (nfs/smb), virtual (tmpfs/proc/sys)
   - [ ] Filter out pseudo-filesystems by default

2. **Path protection** (`src/core/protections.*`)
   - [ ] Define hard blacklist: `/boot`, `/efi`, `/usr`, `/etc`, `/var/lib`, `/proc`, `/sys`, `/dev`, `/run`, `/snap`
   - [ ] Define sensitive dirs with warnings (e.g. `/home/*/.config`, but allow caches)
   - [ ] Check if path is under protected root
   - [ ] Check if path is system-critical
   - [ ] Configurable with reset to defaults

3. **Traversal safety** (`src/core/scanner.*`)
   - [ ] Use `std::filesystem` with `directory_options::skip_permission_denied` or `openat` style
   - [ ] Never follow symlinks when scanning (check `is_symlink()`)
   - [ ] Track visited directories to detect loops
   - [ ] Enforce device boundary: only traverse same `st_dev` as root
   - [ ] Skip cross-filesystem unless explicitly requested
   - [ ] Handle permission errors gracefully (collect skipped list)

### Acceptance Criteria
- Cannot traverse into `/proc`, `/sys` when scanning home
- Cross-filesystem links don't cause spanning
- Symlink loops detected and handled
- Protected paths correctly identified
- Unit tests with temp dirs cover edge cases

---

## Phase 3: Filesystem Scanner Implementation
**Goal:** Implement efficient scanner with progress, cancellation, hard-link dedup, and size calculation.  
**Duration:** 2-4 days  
**Status:** ✅ Complete

### Deliverables
- [ ] Recursive directory scanner (worker thread)
- [ ] Progress reporting (files/dirs processed, bytes, ETA)
- [ ] Cancellation support
- [ ] Hard-link detection using (dev, ino)
- [ ] Logical and allocated size calculation
- [ ] Skipped paths tracking with reasons
- [ ] Scan options/config

### Tasks
1. **Core scanner** (`src/core/scanner.cpp/h`)
   - [ ] Non-blocking API: start, cancel, pause/resume (optional)
   - [ ] Callback for progress: (processed, total_estimate, current_path)
   - [ ] Collect FileItem for each regular file/dir
   - [ ] Handle directories, regular files, symlinks separately

2. **Size calculation**
   - [ ] Logical size: `file_size()` or `st_size`
   - [ ] Allocated size: best effort via `st_blocks * 512` (or `stat` block size). Note confidence if unavailable
   - [ ] Handle sparse files (logical >> allocated)
   - [ ] For directories, compute recursive totals post-scan or during

3. **Deduplication & correctness**
   - [ ] Build map: `key(dev, ino) -> count` to detect hard links
   - [ ] If link count > 1, mark as hard-linked and avoid double-counting allocation for "recoverable" calculation
   - [ ] Flag for hard-linked items
   - [ ] CoW/Btrfs: document limitations in confidence

4. **Error handling**
   - [ ] Collect skipped: `{path, reason}` where reason in {PermissionDenied, NotAccessible, Loop, CrossFilesystem, Protected, Other}
   - [ ] Continue on ENOENT, EACCES gracefully
   - [ ] Rate-limit error logging to avoid spam

### Acceptance Criteria
- Scans large directories without UI freeze
- Cancellation works mid-scan
- Hard links don't double-count allocated space
- Skipped paths listed with clear reasons
- Progress updates reasonable
- Unit tests: hard links, sparse files, symlinks, deep trees

---

## Phase 4: Device Discovery & Dashboard Data
**Goal:** List mounted filesystems/devices and compute usage stats for dashboard.  
**Duration:** 1 day  
**Status:** ✅ Complete

### Deliverables
- [ ] Device discovery service
- [ ] Mount list with sizes (total/used/available)
- [ ] Device classification (local vs remote vs virtual)
- [ ] Dashboard-ready data model

### Tasks
1. **Device enumeration** (`src/platform/devices.*`)
   - [ ] Parse `/proc/mounts` / `/proc/self/mountinfo`
   - [ ] For each mount, get capacity via `statvfs`
   - [ ] Compute used = total - free (or available vs used)
   - [ ] Distinguish: physical devices vs partitions (best effort)
   - [ ] Exclude pseudo-FS from device list shown to user

2. **Filtering**
   - [ ] Show only relevant filesystems: ext4/xfs/btrfs/zfs, vfat/exfat (mounted), etc.
   - [ ] Hide proc/sys/tmpfs from selectable scan targets (or clearly mark)
   - [ ] Include removable media (USB) if mounted

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
**Status:** ⏳ In progress

### Deliverables
- [ ] Heuristic rules for common caches
- [ ] Risk classification logic
- [ ] Confidence scoring
- [ ] "Safe to delete?" policy separation (heuristic ≠ proof)
- [ ] Test coverage for rules

### Tasks
1. **Risk taxonomy**
   - [ ] `Safe`: verified disposable regenerable caches with strong signals
   - [ ] `Low`: likely disposable, needs confirmation
   - [ ] `Medium`: ambiguous (e.g. old downloads, build dirs) - requires careful review
   - [ ] `High`: user data or unclear ownership
   - [ ] `SystemProtected`: matches protected paths - never auto-select

2. **Heuristics** (`src/core/recommendations.*`)
   - [ ] Thumbnail caches: `~/.cache/thumbnails/`, `~/.thumbnails/` (age + regenerable)
   - [ ] Browser caches (profile-specific) - mark as low/med
   - [ ] Package manager caches: `~/.cache/pacman/pkg/`? or system - be conservative; system package caches often need package manager
   - [ ] Temp dirs: `/tmp`, `~/.cache/tmp` (if old, user-owned)
   - [ ] Build artifacts: `target/`, `build/`, `CMakeCache.txt`, `*.o` - user projects
   - [ ] APT cache: `/var/cache/apt/archives` - **system**: high risk, must warn; better delegated
   - [ ] Flatpak/Snap caches: treat carefully
   - [ ] Downloads: never assume "old" = safe (user decision)

3. **Safety rules (hard constraints)**
   - [ ] If path under protected system dirs → `SystemProtected`, cannot delete
   - [ ] If path is root-owned and not in explicit safe user caches → at least `Medium` or `High`
   - [ ] Filename alone insufficient (explicitly enforce this)
   - [ ] Symlinks: handle separately, don't follow
   - [ ] If hard-linked and other references exist, note impact

4. **Confidence & estimates**
   - [ ] Track confidence in "recoverable space" (allocation accuracy, CoW, hard links)
   - [ ] Label estimates clearly in UI
   - [ ] Don't claim full physical free if shared

### Acceptance Criteria
- Protected paths never classified as Safe
- Name-only heuristics rejected
- Each candidate has reason + risk + confidence
- Conservative by default (prefer Medium/High over Safe)
- Unit tests cover key cases

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
**Status:** Pending

### Deliverables
- [ ] Config system (JSON/YAML or QSettings)
- [ ] Audit logger (append-only JSON)
- [ ] Error reporting helpers
- [ ] Path utilities (normalize, canonicalize carefully)
- [ ] String/format helpers for sizes

### Tasks
1. **Configuration** (`src/utils/config.*`)
   - [ ] Default protected paths, exclude patterns
   - [ ] UI prefs (sort, columns, confirm thresholds)
   - [ ] Load/save to `~/.config/driverse/` or XDG

2. **Audit logging** (`src/utils/audit.*`)
   - [ ] Append-only: timestamp, user, action, paths[], sizes, risk, result
   - [ ] Write to `~/.local/share/driverse/audit.jsonl`
   - [ ] Include scan id, duration, errors
   - [ ] Human-readable + machine-parseable

3. **Logging framework**
   - [ ] Simple logger (file + console in debug)
   - [ ] Log levels: Debug, Info, Warn, Error
   - [ ] Avoid logging sensitive full paths in debug? maybe truncate

### Acceptance Criteria
- Config persists across runs
- Audit log records all cleanup operations
- Logs don't leak excessively

---

## Phase 8: Qt GUI - Dashboard & Device Selection
**Goal:** Build main UI with device list, storage overview, and scan controls.  
**Duration:** 1-2 days  
**Status:** Pending

### Deliverables
- [ ] Main window with layout
- [ ] Device list/table (mount, fs, total/used/free, %)
- [ ] Storage utilization chart (Qt Charts)
- [ ] Scan controls (Select dir, Scan, Cancel)
- [ ] Status bar (progress, stats)
- [ ] Menu/actions

### Tasks
1. **Main window** (`src/gui/mainwindow.*`)
   - [ ] QMainWindow with splitter
   - [ ] Left: device/filesystem panel
   - [ ] Right: dashboard area
   - [ ] Bottom: status/progress

2. **Device model/view**
   - [ ] `DeviceTableModel` inheriting `QAbstractTableModel`
   - [ ] Show: Device/Mount, Type, Total, Used, Free, Usage%
   - [ ] Selection enables "Scan Selected"
   - [ ] Visual indicators for virtual/remote

3. **Dashboard widgets**
   - [ ] Donut/pie chart for usage
   - [ ] Summary cards: Total, Used, Free, Est. Recoverable
   - [ ] "Largest dirs preview" (post-scan)
   - [ ] "Scan" button with path selection

4. **Threading**
   - [ ] Use `QThread` or `QtConcurrent` for scanning
   - [ ] Emit signals for progress/results
   - [ ] Keep UI responsive, handle cancellation

### Acceptance Criteria
- Device list populates on startup
- Can select mount point and start scan
- Progress shown during scan
- Cancel button works
- UI remains responsive

---

## Phase 9: Results View - Files, Dirs, & Recommendations
**Goal:** Display scan results with sorting, filtering, and risk classification.  
**Duration:** 1-2 days  
**Status:** Pending

### Deliverables
- [ ] Results tabs: "Largest Files", "Largest Directories", "Recommendations"
- [ ] Sortable/filterable table
- [ ] Risk badges/colors
- [ ] Path actions (Open containing dir, Copy path)
- [ ] Details panel

### Tasks
1. **Results table model** (`src/gui/scanmodel.*`)
   - [ ] Virtual model for large datasets (handle 10^5+ rows)
   - [ ] Columns: Name, Path, Type, Logical Size, Allocated Size, Modified, Risk
   - [ ] Sort by any column
   - [ ] Role-based formatting (sizes, dates)

2. **Filtering**
   - [ ] Filter by risk level (Safe/Low/Med/High/Protected)
   - [ ] Filter by path substring
   - [ ] Filter by min size
   - [ ] "Show only recommended for cleanup"

3. **Recommendations tab**
   - [ ] Group by risk
   - [ ] Show justification for each
   - [ ] Show estimated recoverable
   - [ ] Selection checkboxes (multi-select)
   - [ ] "Select all in group" with caution

4. **Interactions**
   - [ ] Double-click to open in file manager (`xdg-open` parent)
   - [ ] Copy path to clipboard
   - [ ] Show tooltips with details

### Acceptance Criteria
- Large results load smoothly (virtual model)
- Sorting/filtering fast enough
- Risk clearly visible
- Path actions work
- Can inspect items before cleanup

---

## Phase 10: Cleanup Workflow - Dry-Run, Confirmation, & Execution
**Goal:** Implement safe cleanup with mandatory dry-run and confirmations.  
**Priority:** **CRITICAL** (Safety)  
**Duration:** 1-2 days  
**Status:** Pending

### Deliverables
- [ ] Dry-run preview dialog
- [ ] Risk-based confirmation system
- [ ] Cleanup executor (non-destructive simulation first)
- [ ] Trash integration (user files)
- [ ] Audit logging of all actions
- [ ] Error handling & partial results

### Tasks
1. **Dry-run enforcement**
   - [ ] All operations start as dry-run
   - [ ] Preview shows: count, total est. freed, risk breakdown, top items
   - [ ] Must confirm each risk group or overall
   - [ ] Compare against protected paths (re-validate)

2. **Confirmation UI**
   - [ ] For `Safe`/`Low`: checkbox confirmation
   - [ ] For `Medium`: stronger confirmation + "I understand" checkbox
   - [ ] For `High`: require explicit checkbox + list review
   - [ ] For `SystemProtected`: never allow
   - [ ] Option to "Review selected" before proceeding
   - [ ] Type-to-confirm for High risk if root-owned paths selected

3. **Cleanup execution** (`src/platform/trash.*`, `src/core/cleanup.*`)
   - [ ] Prefer XDG Trash: move user-owned files to trash (`gio trash`, `trash-put`, or manual XDG spec)
   - [ ] For regenerable caches in safe locations, allow permanent delete ONLY if explicitly opted-in AND classified Safe
   - [ ] Never delete if path became protected between preview and exec (recheck)
   - [ ] Never follow symlinks
   - [ ] Handle directories: remove empty dirs only if safe and user-approved
   - [ ] Atomic-ish batch: process in order, log each
   - [ ] Track success/failure/skipped

4. **TOCTOU safety**
   - [ ] Snapshot (path, dev, ino, size) at preview time
   - [ ] On delete: verify still exists, same inode/dev (where possible), size not drastically changed?
   - [ ] If drift detected: warn, skip or abort batch
   - [ ] Abort entire batch on critical mismatch

5. **Audit trail**
   - [ ] Log: `{"ts":..., "action":"cleanup", "mode":"dryrun|apply", "user":..., "items":[...], "total_freed":..., "result":"ok|partial|failed", "errors":[]}`
   - [ ] Write to audit log before and after

### Acceptance Criteria
- Dry-run is mandatory (no direct delete without preview)
- Protected paths cannot be deleted
- Trash used for user personal files by default
- Permanent delete requires explicit opt-in
- TOCTOU check prevents deleting wrong file on mismatch
- Audit log complete
- Partial failures reported clearly

---

## Phase 11: Polish, Error Handling & Edge Cases
**Goal:** Improve UX, robustness, and safety for real-world use.  
**Duration:** 1 day  
**Status:** Pending

### Deliverables
- [ ] Improved error messages
- [ ] "Skipped paths" dialog with reasons
- [ ] Empty state messages
- [ ] Tooltips and help text
- [ ] Keyboard shortcuts
- [ ] About dialog

### Tasks
1. **UX polish**
   - [ ] Size formatting (B, KiB, MiB, GiB, TiB) with locale-aware
   - [ ] Date formatting
   - [ ] Loading spinners/progress bars
   - [ ] Risk color coding (consistent palette)
   - [ ] "What's this?" tooltips explaining estimates

2. **Robustness**
   - [ ] Handle unmounted paths
   - [ ] Handle permission denied everywhere
   - [ ] Handle disk full during cleanup (ENOSPC)
   - [ ] Handle busy files (EBUSY)
   - [ ] Handle read-only FS (EROFS)
   - [ ] Network mounts: warn or be cautious

3. **Safety review**
   - [ ] Re-validate all protected checks
   - [ ] Ensure no root traversal via `..` issues (canonicalize but be careful with symlinks)
   - [ ] Test with fake protected paths
   - [ ] Verify audit log integrity

### Acceptance Criteria
- Graceful handling of common errors
- Clear user feedback
- No crashes on edge cases
- Safety checks comprehensive

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

## Next Steps

**Start with Phase 0.** Let's set up the CMake project structure, configure Qt6, and get a minimal app building.

Would you like to begin Phase 0 now? I can create all the files and get the first build working.