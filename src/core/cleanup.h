#pragma once

#include "types.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace driverse::core {

// Cleanup is always planned in dry-run first; only an explicit Apply touches
// the filesystem.
enum class CleanupMode {
    DryRun,
    Apply
};

// What the executor intends to do with an item.
enum class CleanupAction {
    MoveToTrash,     // reversible (XDG Trash)
    PermanentDelete, // only for opted-in, user-owned, regenerable Safe caches
    Skip             // never touched; `skipReason` explains why
};

// One item in a cleanup plan: the preview-time snapshot plus the decision.
// (path, deviceId, inode) identify exactly what was seen at preview time so the
// executor can detect substitutions before acting (TOCTOU).
struct CleanupItem {
    std::filesystem::path path;
    std::uintmax_t deviceId = 0;
    std::uintmax_t inode = 0;
    std::uintmax_t allocatedSize = 0; // estimated reclaimable bytes
    std::uintmax_t logicalSize = 0;
    bool isDirectory = false;
    bool userOwned = false;
    std::uintmax_t ownerUid = 0;
    RiskLevel risk = RiskLevel::Medium;
    CleanupCategory category = CleanupCategory::Unknown;
    bool permanentDeleteAllowed = false;
    CleanupAction action = CleanupAction::Skip;
    std::string skipReason;
};

struct CleanupOptions {
    CleanupMode mode = CleanupMode::DryRun;
    // Opt-in: allow permanent deletion of eligible items instead of trashing.
    bool allowPermanentDelete = false;
    // TOCTOU policy: abort the whole batch on an identity mismatch (default) or
    // just skip the affected item.
    bool abortOnDrift = true;
    // Trash root override (empty = XDG default). Tests point this at a temp dir.
    std::filesystem::path trashRoot;
    // Current user's uid (0 = use getuid()). Used for the ownership rule.
    std::uintmax_t currentUserUid = 0;
    // Home directory; guards against acting on home itself or an ancestor.
    std::filesystem::path homeDirectory;
};

struct CleanupPlan {
    std::vector<CleanupItem> items;
    std::uintmax_t totalBytes = 0; // sum over actionable items
    std::size_t actionableCount = 0;
    std::size_t skippedCount = 0;
    std::array<std::uintmax_t, 5> countByRisk{}; // index = static_cast<int>(RiskLevel)
    std::array<std::uintmax_t, 5> bytesByRisk{};
};

enum class CleanupStatus {
    DryRun,        // simulated, nothing changed
    MovedToTrash,  // recoverable
    Deleted,       // permanently removed
    Skipped,       // deliberately not touched
    Failed         // attempted but errored
};

struct CleanupItemResult {
    std::filesystem::path path;
    CleanupStatus status = CleanupStatus::Skipped;
    std::string detail; // reason / error / note
    std::uintmax_t bytes = 0;
};

struct CleanupReport {
    CleanupMode mode = CleanupMode::DryRun;
    std::vector<CleanupItemResult> items;
    std::uintmax_t bytesPlanned = 0;
    std::uintmax_t bytesFreed = 0;   // permanently deleted (actually reclaimed)
    std::uintmax_t bytesTrashed = 0; // moved to trash (recoverable, not reclaimed)
    std::size_t dryRun = 0;
    std::size_t moved = 0;
    std::size_t deleted = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
    bool aborted = false;
    std::string abortReason;
    std::chrono::milliseconds duration{0};

    bool ok() const { return !aborted && failed == 0; }
};

// Snapshot the selected candidates into an executable plan and decide the
// action for each. Rejects system-protected paths, non-user-owned items, and
// dangerous roots (home or an ancestor of it). Never touches the filesystem.
CleanupPlan buildCleanupPlan(const std::vector<CleanupCandidate>& candidates,
                             const CleanupOptions& options);

// Execute (or simulate) a plan. Re-validates protected paths and verifies the
// (dev, ino) identity of every item before acting. In DryRun mode nothing is
// modified; in Apply mode items are moved to the trash or, if explicitly
// opted-in and eligible, permanently deleted.
CleanupReport executeCleanup(const CleanupPlan& plan, const CleanupOptions& options);

std::string cleanupStatusToString(CleanupStatus status);
std::string cleanupActionToString(CleanupAction action);

} // namespace driverse::core
