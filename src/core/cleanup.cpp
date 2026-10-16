#include "cleanup.h"

#include "platform/trash.h"
#include "protections.h"
#include "utils.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace driverse::core {

namespace {

std::chrono::milliseconds elapsed(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
}

std::uintmax_t effectiveUid(const CleanupOptions& options) {
    return options.currentUserUid != 0 ? options.currentUserUid
                                       : static_cast<std::uintmax_t>(::getuid());
}

bool isProtectedNow(const std::filesystem::path& path) {
    return utils::isProtectedPath(path) || Protections::instance().isProtected(path);
}

// Hard safety net: refuse to act on "/", on the home directory itself, or on an
// ancestor of it (e.g. /home). The recommendation engine should never propose
// these, but the executor does not rely on that.
bool isDangerousRoot(const std::filesystem::path& path,
                     const std::filesystem::path& home) {
    if (path.empty()) {
        return true;
    }
    if (path == path.root_path()) {
        return true;
    }
    if (!home.empty() && utils::isPathUnder(home, path)) {
        return true; // home itself or an ancestor of home
    }
    return false;
}

} // namespace

CleanupPlan buildCleanupPlan(const std::vector<CleanupCandidate>& candidates,
                             const CleanupOptions& options) {
    CleanupPlan plan;
    const std::uintmax_t uid = effectiveUid(options);
    const std::filesystem::path home = options.homeDirectory;

    for (const auto& candidate : candidates) {
        CleanupItem item;
        item.path = utils::canonicalizeNoFollow(candidate.path);
        item.risk = candidate.risk;
        item.category = candidate.category;
        item.ownerUid = candidate.ownerUid;
        item.permanentDeleteAllowed = candidate.permanentDeleteAllowed;
        item.allocatedSize = candidate.allocatedSize;
        item.logicalSize = candidate.logicalSize;
        item.isDirectory = candidate.isDirectory;

        auto skip = [&plan, &item](std::string reason) {
            item.action = CleanupAction::Skip;
            item.skipReason = std::move(reason);
            plan.items.push_back(std::move(item));
            ++plan.skippedCount;
        };

        if (isDangerousRoot(item.path, home)) {
            skip("refusing to touch a root or home path");
            continue;
        }
        if (isProtectedNow(item.path)) {
            skip("protected path");
            continue;
        }
        if (candidate.risk == RiskLevel::SystemProtected) {
            skip("system-protected");
            continue;
        }

        struct stat st {};
        if (::lstat(item.path.c_str(), &st) != 0) {
            skip("no longer exists");
            continue;
        }

        item.deviceId = static_cast<std::uintmax_t>(st.st_dev);
        item.inode = static_cast<std::uintmax_t>(st.st_ino);
        item.isDirectory = S_ISDIR(st.st_mode);
        item.ownerUid = static_cast<std::uintmax_t>(st.st_uid);
        item.userOwned = (item.ownerUid == uid);

        if (!item.userOwned) {
            skip("not owned by the current user");
            continue;
        }

        if (options.allowPermanentDelete && candidate.risk == RiskLevel::Safe &&
            candidate.permanentDeleteAllowed) {
            item.action = CleanupAction::PermanentDelete;
        } else {
            item.action = CleanupAction::MoveToTrash;
        }

        const auto index = static_cast<std::size_t>(item.risk);
        plan.countByRisk[index] += 1;
        plan.bytesByRisk[index] += item.allocatedSize;
        plan.totalBytes += item.allocatedSize;
        ++plan.actionableCount;
        plan.items.push_back(std::move(item));
    }

    return plan;
}

CleanupReport executeCleanup(const CleanupPlan& plan, const CleanupOptions& options) {
    const auto start = std::chrono::steady_clock::now();

    CleanupReport report;
    report.mode = options.mode;
    report.bytesPlanned = plan.totalBytes;

    const bool applying = options.mode == CleanupMode::Apply;
    const std::filesystem::path home = options.homeDirectory;
    platform::Trash trash(options.trashRoot);

    if (applying) {
        const bool needsTrash =
            std::any_of(plan.items.begin(), plan.items.end(), [](const CleanupItem& item) {
                return item.action == CleanupAction::MoveToTrash;
            });
        if (needsTrash) {
            std::string error;
            if (!trash.ensureReady(&error)) {
                report.aborted = true;
                report.abortReason = error.empty() ? "trash is unavailable" : error;
                report.duration = elapsed(start);
                return report;
            }
        }
    }

    for (std::size_t i = 0; i < plan.items.size(); ++i) {
        const CleanupItem& item = plan.items[i];
        CleanupItemResult result;
        result.path = item.path;
        result.bytes = item.allocatedSize;

        if (item.action == CleanupAction::Skip) {
            result.status = CleanupStatus::Skipped;
            result.detail = item.skipReason.empty() ? "skipped" : item.skipReason;
            ++report.skipped;
            report.items.push_back(std::move(result));
            continue;
        }

        // --- Re-validate everything at execution time ---
        if (isDangerousRoot(item.path, home) || isProtectedNow(item.path)) {
            result.status = CleanupStatus::Skipped;
            result.detail = "protected path (re-checked at execution)";
            ++report.skipped;
            report.items.push_back(std::move(result));
            continue;
        }

        struct stat st {};
        if (::lstat(item.path.c_str(), &st) != 0) {
            result.status = CleanupStatus::Skipped;
            result.detail = "disappeared before execution";
            ++report.skipped;
            report.items.push_back(std::move(result));
            continue;
        }

        const auto dev = static_cast<std::uintmax_t>(st.st_dev);
        const auto ino = static_cast<std::uintmax_t>(st.st_ino);
        const bool isDir = S_ISDIR(st.st_mode);
        if (dev != item.deviceId || ino != item.inode || isDir != item.isDirectory) {
            const std::string reason =
                "identity changed since preview (dev/inode/type mismatch)";
            result.status = CleanupStatus::Skipped;
            result.detail = reason;
            ++report.skipped;
            report.items.push_back(std::move(result));

            if (options.abortOnDrift) {
                report.aborted = true;
                report.abortReason = reason + ": " + item.path.string();
                for (std::size_t j = i + 1; j < plan.items.size(); ++j) {
                    CleanupItemResult rest;
                    rest.path = plan.items[j].path;
                    rest.bytes = plan.items[j].allocatedSize;
                    rest.status = CleanupStatus::Skipped;
                    rest.detail = "batch aborted";
                    ++report.skipped;
                    report.items.push_back(std::move(rest));
                }
                break;
            }
            continue;
        }

        if (!isDir && static_cast<std::uintmax_t>(st.st_size) != item.logicalSize) {
            result.detail = "note: size changed since preview";
        }

        if (!applying) {
            result.status = CleanupStatus::DryRun;
            if (result.detail.empty()) {
                result.detail = cleanupActionToString(item.action);
            }
            ++report.dryRun;
            report.items.push_back(std::move(result));
            continue;
        }

        if (item.action == CleanupAction::MoveToTrash) {
            const platform::TrashMoveResult moved = trash.move(item.path);
            if (moved.ok) {
                result.status = CleanupStatus::MovedToTrash;
                result.detail = "moved to trash: " + moved.trashedPath.string();
                report.bytesTrashed += item.allocatedSize;
                ++report.moved;
            } else {
                result.status = CleanupStatus::Failed;
                result.detail = moved.error;
                ++report.failed;
            }
        } else {
            std::error_code ec;
            if (isDir) {
                std::filesystem::remove_all(item.path, ec);
            } else {
                std::filesystem::remove(item.path, ec);
            }
            if (ec) {
                result.status = CleanupStatus::Failed;
                result.detail = "delete failed: " + ec.message();
                ++report.failed;
            } else {
                result.status = CleanupStatus::Deleted;
                result.detail = "permanently deleted";
                report.bytesFreed += item.allocatedSize;
                ++report.deleted;
            }
        }
        report.items.push_back(std::move(result));
    }

    report.duration = elapsed(start);
    return report;
}

std::string cleanupStatusToString(CleanupStatus status) {
    switch (status) {
        case CleanupStatus::DryRun:       return "dry-run";
        case CleanupStatus::MovedToTrash: return "trashed";
        case CleanupStatus::Deleted:      return "deleted";
        case CleanupStatus::Skipped:      return "skipped";
        case CleanupStatus::Failed:       return "failed";
    }
    return "unknown";
}

std::string cleanupActionToString(CleanupAction action) {
    switch (action) {
        case CleanupAction::MoveToTrash:     return "move to trash";
        case CleanupAction::PermanentDelete: return "permanently delete";
        case CleanupAction::Skip:            return "skip";
    }
    return "skip";
}

} // namespace driverse::core
