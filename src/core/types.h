#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace driverse::core {

enum class RiskLevel {
    Safe,           // Verified safe to remove (regenerable, disposable)
    Low,            // Likely safe, needs confirmation
    Medium,         // Ambiguous - requires careful review
    High,           // User data or unclear - needs strong confirmation
    SystemProtected // System critical - never allow deletion
};

enum class ConfidenceLevel {
    High,           // High confidence in estimate
    Medium,         // Medium confidence
    Low             // Low confidence (CoW, sparse, shared deps)
};

enum class FileType {
    RegularFile,
    Directory,
    Symlink,
    Other
};

enum class SkipReason {
    PermissionDenied,
    NotAccessible,
    LoopDetected,
    CrossFilesystem,
    ProtectedPath,
    IsSymlink,
    PseudoFilesystem,
    Other
};

// Classification of a cleanup candidate (used by the recommendation engine).
// Mirrors the heuristic categories; conservative by design — anything not
// matched by a known rule stays Unknown and is never auto-recommended.
enum class CleanupCategory {
    Unknown,
    ThumbnailCache,     // e.g. ~/.cache/thumbnails (regenerable)
    BrowserCache,       // Firefox/Chrome/Chromium/Brave caches
    PackageCache,       // pacman/APT downloaded package caches
    TempCache,          // /tmp, ~/.cache/tmp (short-lived)
    BuildArtifact,      // build/, target/, *.o, node_modules (user projects)
    ApplicationCache,   // generic ~/.cache/<app>
    LogFile,            // *.log
    Downloads,          // ~/Downloads (user data, high risk)
    SystemCache         // /var/cache/apt/archives etc. (delegate to PM)
};

// File item metadata
struct FileItem {
    std::filesystem::path path;
    std::filesystem::path name;
    std::uintmax_t logicalSize;      // Logical size in bytes
    std::uintmax_t allocatedSize;    // Allocated size on disk (best effort)
    std::chrono::system_clock::time_point mtime;
    FileType type;
    std::uintmax_t inode;
    std::uintmax_t deviceId;         // st_dev
    std::uintmax_t ownerUid;         // st_uid — used for ownership safety rules
    bool isSymlink;
    bool isDirectory;
    bool isHardLinked;
    bool isProtected;
    bool isSparse;
    int hardLinkCount;               // Number of hard links
    ConfidenceLevel sizeConfidence;  // Confidence in allocated size
};

// Skipped path info
struct SkippedPath {
    std::filesystem::path path;
    SkipReason reason;
    std::string reasonString;
};

// Device/mount info
struct DeviceInfo {
    std::string device;              // Device name (e.g., /dev/sda1)
    std::string mountPoint;          // Mount point
    std::string fsType;              // Filesystem type
    std::string options;             // Mount options
    std::uintmax_t totalBytes = 0;
    std::uintmax_t usedBytes = 0;
    std::uintmax_t freeBytes = 0;
    std::uintmax_t availableBytes = 0;   // Available to user
    bool isPseudoFs = false;             // proc/sys/tmpfs etc
    bool isNetworkFs = false;            // nfs/smb etc
    bool isReadOnly = false;
    std::uintmax_t deviceId = 0;         // st_dev

    // Backing block device classification (G2), resolved via sysfs.
    std::string parentDisk;              // top-level disk ("sda"); "" if unknown
    bool isPartition = false;            // partition of `parentDisk`
    bool isRemovable = false;            // removable media (USB stick etc)
};

// Cleanup candidate
struct CleanupCandidate {
    std::filesystem::path path;
    CleanupCategory category = CleanupCategory::Unknown;
    std::uintmax_t logicalSize = 0;
    std::uintmax_t allocatedSize = 0; // Estimated RECOVERABLE bytes (conservative)
    RiskLevel risk = RiskLevel::Medium;
    ConfidenceLevel confidence = ConfidenceLevel::Medium;
    std::string justification;        // Why it's recommended
    std::vector<std::string> tags;    // cache, thumbnail, build, etc.
    bool isUserOwned = false;
    bool isDirectory = false;
    bool permanentDeleteAllowed = false; // True only for regenerable, user-owned caches
    std::uintmax_t ownerUid = 0;
    std::chrono::system_clock::time_point mtime{};
};

// Directory aggregate summary
struct DirectorySummary {
    std::filesystem::path path;
    std::uintmax_t logicalSize;
    std::uintmax_t allocatedSize;         // Sum of allocated (may double-count hard links)
    std::uintmax_t uniqueAllocatedSize;   // Hard-link deduplicated
    std::uintmax_t fileCount;
    std::uintmax_t dirCount;
    std::size_t depth;
    std::uintmax_t ownerUid = 0;          // st_uid — for ownership safety rules
    std::chrono::system_clock::time_point mtime{};
};

// Scan statistics
struct ScanStats {
    std::uintmax_t filesScanned = 0;
    std::uintmax_t dirsScanned = 0;
    std::uintmax_t symlinksSkipped = 0;
    std::uintmax_t hardLinksDeduplicated = 0;
    std::uintmax_t skippedCount = 0;
    std::uintmax_t totalLogicalBytes = 0;
    std::uintmax_t totalAllocatedBytes = 0;
    std::uintmax_t uniqueAllocatedBytes = 0; // After hard-link dedup
    std::chrono::milliseconds duration{0};
};

// Aggregated scan output
struct ScanResult {
    std::filesystem::path root;
    std::vector<FileItem> largestFiles;        // Bounded top-N by allocated size
    std::vector<DirectorySummary> directories; // All directories encountered
    std::vector<SkippedPath> skipped;          // Errors / protected / cross-fs
    ScanStats stats;
    bool cancelled = false;
    bool completed = false;
    std::string error;                         // Fatal error message (empty if none)
};

} // namespace driverse::core
