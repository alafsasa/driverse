#pragma once

#include "types.h"
#include <string>
#include <system_error>
#include <vector>
#include <filesystem>

namespace driverse::core::utils {

// Format bytes to human readable string
std::string formatBytes(std::uintmax_t bytes);

// Format duration in milliseconds
std::string formatDuration(std::chrono::milliseconds ms);

// Human-readable message for an errno value (e.g. EACCES -> "Permission denied").
std::string describeErrno(int err);

// Friendly, actionable hint for common filesystem error codes (ENOSPC, EBUSY,
// EROFS, EACCES, ...). Returns an empty string when there is nothing useful to
// add beyond the error code's own message.
std::string fsErrorHint(const std::error_code& ec);

// Abbreviate a long path for logging by eliding the middle:
//   /home/alice/very/long/.../file.txt  ->  "/home/alice/.../file.txt"
// Keeps the leading root and the trailing components. maxLen == 0 disables.
std::string abbreviatePath(const std::filesystem::path& path, std::size_t maxLen = 80);

// Check if path is under a protected root
bool isProtectedPath(const std::filesystem::path& path);

// Get default protected paths
std::vector<std::filesystem::path> getDefaultProtectedPaths();

// Get default excluded paths/patterns (pseudo-filesystems)
std::vector<std::filesystem::path> getDefaultExcludedPaths();

// Get default excluded filesystem types
std::vector<std::string> getDefaultExcludedFsTypes();

// Normalize path (resolve .. where safe, but don't follow symlinks)
std::filesystem::path normalizePath(const std::filesystem::path& path);

// Absolute + lexically-normal string form of `path`. Fast-paths paths that
// already are absolute and lexically normal (the common case for scanned
// entries), avoiding the costly re-parse of absolute()/lexically_normal().
std::string normalizedAbsoluteString(const std::filesystem::path& path);

// True if `child` equals or is nested under `parent`, compared component-wise
// without resolving symlinks (so "/etcetera" is NOT under "/etc").
bool isPathUnder(const std::filesystem::path& child, const std::filesystem::path& parent);

// Fast component-boundary prefix test for paths that are already absolute and
// lexically normalized (in std::string form). Equivalent to isPathUnder() but
// avoids re-normalizing both sides; used on hot paths such as the per-entry
// protected-path checks during a scan.
bool isPathUnderNormalized(const std::string& child, const std::string& parent);

// Canonicalize a path for safe operations: resolve symlinks in the existing
// ancestor components but do NOT follow a symlink in the final component.
// Use this before deleting, so we act on the link itself, not its target.
std::filesystem::path canonicalizeNoFollow(const std::filesystem::path& path);

// Convert skip reason to string
std::string skipReasonToString(SkipReason reason);

// Convert risk level to string
std::string riskLevelToString(RiskLevel risk);

// Convert confidence level to string
std::string confidenceLevelToString(ConfidenceLevel confidence);

// Check if filesystem is pseudo
bool isPseudoFilesystem(const std::string& fsType);

// Check if filesystem is network
bool isNetworkFilesystem(const std::string& fsType);

} // namespace driverse::core::utils
