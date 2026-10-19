#pragma once

#include "types.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

namespace driverse::core {

struct ScanOptions {
    std::filesystem::path root;
    bool followSymlinks = false;         // Never follow by default
    bool oneFilesystem = true;           // Stay on the root's st_dev
    bool skipProtectedPaths = true;      // Do not descend into system-protected dirs
    bool measureAllocatedSize = true;    // Use st_blocks * 512
    std::size_t maxDepth = 0;            // 0 = unlimited
    std::size_t maxRetainedFiles = 5000; // Top-N largest files kept
    std::vector<std::filesystem::path> excludePaths;

    // G3: bounded pre-count pass. Walks the tree once without metadata to
    // learn the total directory/file counts, so progress can show a real
    // percentage and an ETA. Abandoned when the budget is exceeded (or the
    // pass is disabled), in which case progress falls back to counts-only.
    bool estimateTotals = true;
    std::chrono::milliseconds estimateBudget{1500};
};

struct ScanProgress {
    std::uintmax_t filesScanned = 0;
    std::uintmax_t dirsScanned = 0;
    std::uintmax_t bytesSeen = 0;
    int currentDepth = 0;
    std::filesystem::path currentPath;

    // G3: totals from the pre-count pass (0 = unknown/not yet counted).
    // `counting` is true while that pass is running.
    std::uintmax_t totalDirs = 0;
    std::uintmax_t totalFiles = 0;
    bool counting = false;
};

// Thread-confined scanner. Call scan() from a worker thread.
// Progress/item callbacks are invoked on the calling thread.
class Scanner {
public:
    using ProgressCallback = std::function<void(const ScanProgress&)>;
    using ItemCallback = std::function<void(const FileItem&)>;
    using DirectedCallback = std::function<void(const DirectorySummary&)>;

    Scanner() = default;

    void setOptions(const ScanOptions& options);
    const ScanOptions& options() const;

    void setProgressCallback(ProgressCallback cb);
    void setItemCallback(ItemCallback cb);
    void setDirectoryCallback(DirectedCallback cb);

    // Request cancellation. Safe to call from another thread.
    void cancel();
    bool isCancelled() const;

    // Blocking scan. Returns the aggregated result.
    ScanResult scan();

private:
    ScanOptions m_options;
    ProgressCallback m_progressCb;
    ItemCallback m_itemCb;
    DirectedCallback m_dirCb;
    std::atomic<bool> m_cancelled{false};
};

} // namespace driverse::core
