# Driverse - Linux Storage Cleaner

Driverse is a safe and conservative Linux desktop application designed to help users monitor storage devices, understand disk usage, locate large files and applications, and identify opportunities to recover disk space safely.

## Core Principles

- **Safety First**: Conservative recommendations, explicit user approval, and protection of system-critical files
- **Transparent**: Clear distinction between logical and allocated sizes, hard links handled correctly
- **Conservative**: Never assume "cache" or "tmp" names mean safe to remove
- **User-Controlled**: All destructive operations require dry-run preview and confirmation
- **Least Privilege**: Runs unprivileged by default

## Technology Stack

- **Language**: C++20
- **GUI Framework**: Qt 6 (Widgets)
- **Build System**: CMake 3.20+
- **Target**: Linux Desktop

## Build Instructions

### Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+ or Clang 12+)
- Qt6 (Core, Widgets, Concurrent)

### Building

```bash
mkdir build
cd build
cmake ..
make -j4
```

### Running

```bash
./driverse
```

## Project Status

**Current Phase:** Phases 0–5 and 7–9 complete (foundation, core models,
safety/mount detection, scanner, device data layer, recommendation engine, the
config/audit/logging integration layer, the Qt GUI dashboard with device
selection and cancellable background scans, and the results view with sortable/
filterable largest-files, largest-directories and recommendation tables). Phase
10 (cleanup workflow) is next.

**Tests:** `ctest` runs the suites in `tests/` (types, utils, mounts, scanner,
scanner_userns, recommendations, config, audit, integration, logger,
devicetablemodel, scanmodel, scanworker).

**GUI:** The dashboard runs headless for smoke tests with
`QT_QPA_PLATFORM=offscreen ./driverse`. Scanning is read-only; the
Recommendations tab only selects candidates — nothing is deleted.

See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the full roadmap and known gaps.

## Safety Features (Planned)

- Protected paths denylist (prevents accidental deletion of system files)
- Mount boundary enforcement (no cross-filesystem traversal)
- Hard link detection (prevents double-counting)
- Dry-run mandatory before any destructive operation
- Risk-based confirmation system
- XDG Trash integration (reversible by default)
- Audit logging of all operations

## License

[To be determined]
