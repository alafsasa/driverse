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

// File item metadata
struct FileItem {
    std::filesystem::path path;
    std::filesystem::path name;
    std::uintmax_t logicalSize;      // Logical size in bytes
    std::uintmax_t allocatedSize;    // Allocated size on disk (best effort)
    std::filesystem::file_time_type mtime;
    FileType type;
    std::uintmax_t inode;
    std::uintmax_t deviceId;         // st_dev
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
    std::uintmax_t totalBytes;
    std::uintmax_t usedBytes;
    std::uintmax_t freeBytes;
    std::uintmax_t availableBytes;   // Available to user
    bool isPseudoFs;                 // proc/sys/tmpfs etc
    bool isNetworkFs;                // nfs/smb etc
    bool isReadOnly;
    std::uintmax_t deviceId;         // st_dev
};

// Cleanup candidate
struct CleanupCandidate {
    std::filesystem::path path;
    std::uintmax_t logicalSize;
    std::uintmax_t allocatedSize;    // Estimated recoverable
    RiskLevel risk;
    ConfidenceLevel confidence;
    std::string justification;       // Why it's recommended
    std::vector<std::string> tags;   // cache, thumbnail, build, etc.
    bool isUserOwned;
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
