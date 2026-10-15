#include "scanner.h"
#include "utils.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <queue>
#include <unordered_set>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

namespace driverse::core {

namespace {

// 512-byte units, per POSIX (st_blocks).
constexpr std::uintmax_t kBlockUnit = 512;

// Cap the number of individual skipped entries retained to bound memory.
constexpr std::size_t kMaxRecordedSkips = 50000;

// Emit progress at most every N entries to avoid callback overhead.
constexpr std::uintmax_t kProgressInterval = 512;

// Compose a (dev, ino) identity key for hard-link deduplication.
struct InodeKey {
    std::uintmax_t dev;
    std::uintmax_t ino;
    bool operator==(const InodeKey& o) const { return dev == o.dev && ino == o.ino; }
};

struct InodeKeyHash {
    std::size_t operator()(const InodeKey& k) const {
        return std::hash<std::uintmax_t>()(k.dev) ^ (std::hash<std::uintmax_t>()(k.ino) << 1);
    }
};

// Min-heap comparator: keeps smallest allocated size on top.
struct LargerOnTop {
    bool operator()(const FileItem& a, const FileItem& b) const {
        return a.allocatedSize > b.allocatedSize;
    }
};

struct DirTotals {
    std::uintmax_t logical = 0;
    std::uintmax_t allocated = 0;
    std::uintmax_t uniqueAllocated = 0;
    std::uintmax_t fileCount = 0;
    std::uintmax_t dirCount = 0;
};

SkipReason reasonFromErrno(int err) {
    switch (err) {
        case EACCES:
        case EPERM:
            return SkipReason::PermissionDenied;
        case ELOOP:
            return SkipReason::LoopDetected;
        case ENOENT:
        case ENOTDIR:
            return SkipReason::NotAccessible;
        default:
            return SkipReason::Other;
    }
}

bool isUnder(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto p = path.lexically_normal();
    auto r = root.lexically_normal();
    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++pit) {
        if (pit == p.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

ConfidenceLevel confidenceForFilesystemType(const std::filesystem::path& root) {
    struct statfs sfs {};
    if (::statfs(root.c_str(), &sfs) != 0) {
        return ConfidenceLevel::Medium;
    }
    // Robust allocated-size accounting on conventional filesystems. Copy-on-write
    // and compressed filesystems report blocks that do not map cleanly to reclaimable
    // space, so lower confidence there.
    switch (static_cast<unsigned long>(sfs.f_type)) {
        case 0x9123683E: // BTRFS_SUPER_MAGIC
        case 0x2FC12FC1: // ZFS_SUPER_MAGIC
            return ConfidenceLevel::Low;
        default:
            return ConfidenceLevel::High;
    }
}

} // namespace

void Scanner::setOptions(const ScanOptions& options) { m_options = options; }
const ScanOptions& Scanner::options() const { return m_options; }
void Scanner::setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }
void Scanner::setItemCallback(ItemCallback cb) { m_itemCb = std::move(cb); }
void Scanner::setDirectoryCallback(DirectedCallback cb) { m_dirCb = std::move(cb); }
void Scanner::cancel() { m_cancelled.store(true, std::memory_order_relaxed); }
bool Scanner::isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

namespace {

class ScanContext {
public:
    ScanContext(const ScanOptions& opt, ScanResult& res, Scanner* owner)
        : m_opt(opt), m_res(res), m_owner(owner) {
        m_rootDev = 0;
        m_confidence = confidenceForFilesystemType(opt.root);
        for (const auto& e : opt.excludePaths) {
            m_exclude.push_back(std::filesystem::absolute(e).lexically_normal());
        }
    }

    void run() {
        m_res.root = m_opt.root;

        struct stat rootSt {};
        if (::lstat(m_opt.root.c_str(), &rootSt) != 0) {
            m_res.error = "Cannot access " + m_opt.root.string() + ": " + std::strerror(errno);
            return;
        }
        if (!S_ISDIR(rootSt.st_mode)) {
            m_res.error = m_opt.root.string() + " is not a directory";
            return;
        }
        m_rootDev = static_cast<std::uintmax_t>(rootSt.st_dev);

        emitProgress(m_opt.root, 0);
        DirTotals totals = scanDirectory(m_opt.root, 0);
        m_res.stats.totalLogicalBytes = totals.logical;
        m_res.stats.totalAllocatedBytes = totals.allocated;
        m_res.stats.uniqueAllocatedBytes = totals.uniqueAllocated;

        // Flush retained top-N (heap order) into sorted descending order.
        m_res.largestFiles.clear();
        m_res.largestFiles.reserve(m_topFiles.size());
        while (!m_topFiles.empty()) {
            m_res.largestFiles.push_back(m_topFiles.top());
            m_topFiles.pop();
        }
        // Heap pops smallest-first, so reverse to get largest-first.
        std::reverse(m_res.largestFiles.begin(), m_res.largestFiles.end());
    }

private:
    bool excluded(const std::filesystem::path& p) const {
        for (const auto& e : m_exclude) {
            if (isUnder(p, e)) return true;
        }
        return false;
    }

    void recordSkip(const std::filesystem::path& p, SkipReason reason) {
        ++m_res.stats.skippedCount;
        if (m_res.skipped.size() < kMaxRecordedSkips) {
            m_res.skipped.push_back({p, reason, utils::skipReasonToString(reason)});
        }
    }

    void emitProgress(const std::filesystem::path& current, int depth) {
        if (m_progressCb && (++m_progressCounter % kProgressInterval == 0)) {
            ScanProgress p;
            p.filesScanned = m_res.stats.filesScanned;
            p.dirsScanned = m_res.stats.dirsScanned;
            p.bytesSeen = m_res.stats.totalLogicalBytes;
            p.currentDepth = depth;
            p.currentPath = current;
            m_progressCb(p);
        }
    }

    DirTotals scanDirectory(const std::filesystem::path& dir, std::size_t depth) {
        DirTotals totals;

        if (m_owner->isCancelled()) {
            m_res.cancelled = true;
            return totals;
        }

        DIR* dp = ::opendir(dir.c_str());
        if (!dp) {
            recordSkip(dir, reasonFromErrno(errno));
            return totals;
        }
        const int dirFd = ::dirfd(dp);

        ++m_res.stats.dirsScanned;

        struct dirent* entry = nullptr;
        while ((entry = ::readdir(dp)) != nullptr) {
            if (m_owner->isCancelled()) {
                m_res.cancelled = true;
                break;
            }

            const char* name = entry->d_name;
            if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
                continue;
            }

            const std::filesystem::path fullPath = dir / name;

            if (excluded(fullPath)) {
                recordSkip(fullPath, SkipReason::ProtectedPath);
                continue;
            }

            struct stat st {};
            if (::fstatat(dirFd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                recordSkip(fullPath, reasonFromErrno(errno));
                continue;
            }

            if (S_ISLNK(st.st_mode)) {
                // Never follow symlinks by default.
                ++m_res.stats.symlinksSkipped;
                emitProgress(fullPath, static_cast<int>(depth));
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                if (m_opt.oneFilesystem && static_cast<std::uintmax_t>(st.st_dev) != m_rootDev) {
                    recordSkip(fullPath, SkipReason::CrossFilesystem);
                    continue;
                }
                if (m_opt.skipProtectedPaths && fullPath != m_opt.root &&
                    utils::isProtectedPath(fullPath)) {
                    recordSkip(fullPath, SkipReason::ProtectedPath);
                    continue;
                }
                if (m_opt.maxDepth > 0 && depth + 1 >= m_opt.maxDepth) {
                    // Count this directory but do not descend.
                    ++totals.dirCount;
                    emitProgress(fullPath, static_cast<int>(depth));
                    continue;
                }

                DirTotals sub = scanDirectory(fullPath, depth + 1);
                totals.logical += sub.logical;
                totals.allocated += sub.allocated;
                totals.uniqueAllocated += sub.uniqueAllocated;
                totals.fileCount += sub.fileCount;
                totals.dirCount += sub.dirCount + 1;

                DirectorySummary summary;
                summary.path = fullPath;
                summary.logicalSize = sub.logical;
                summary.allocatedSize = sub.allocated;
                summary.uniqueAllocatedSize = sub.uniqueAllocated;
                summary.fileCount = sub.fileCount;
                summary.dirCount = sub.dirCount;
                summary.depth = depth + 1;
                m_res.directories.push_back(summary);
                if (m_dirCb) m_dirCb(summary);
                emitProgress(fullPath, static_cast<int>(depth));
                continue;
            }

            if (S_ISREG(st.st_mode)) {
                FileItem item;
                item.path = fullPath;
                item.name = name;
                item.logicalSize = static_cast<std::uintmax_t>(st.st_size);
                item.allocatedSize = m_opt.measureAllocatedSize
                    ? static_cast<std::uintmax_t>(st.st_blocks) * kBlockUnit
                    : item.logicalSize;
                item.isSparse = item.allocatedSize < item.logicalSize;
                item.inode = static_cast<std::uintmax_t>(st.st_ino);
                item.deviceId = static_cast<std::uintmax_t>(st.st_dev);
                item.hardLinkCount = static_cast<int>(st.st_nlink);
                item.type = FileType::RegularFile;
                item.isSymlink = false;
                item.isDirectory = false;
                item.isProtected = utils::isProtectedPath(fullPath);
                item.isHardLinked = st.st_nlink > 1;
                item.sizeConfidence = m_confidence;

                std::uintmax_t uniqueContribution = item.allocatedSize;
                if (st.st_nlink > 1) {
                    InodeKey key{item.deviceId, item.inode};
                    auto [it, inserted] = m_seenInodes.insert(key);
                    if (!inserted) {
                        // Already accounted for another link to this inode.
                        uniqueContribution = 0;
                        ++m_res.stats.hardLinksDeduplicated;
                    }
                }

                totals.logical += item.logicalSize;
                totals.allocated += item.allocatedSize;
                totals.uniqueAllocated += uniqueContribution;
                ++totals.fileCount;
                ++m_res.stats.filesScanned;
                m_res.stats.totalLogicalBytes += item.logicalSize;
                m_res.stats.totalAllocatedBytes += item.allocatedSize;
                m_res.stats.uniqueAllocatedBytes += uniqueContribution;

                retainTopFile(item);
                if (m_itemCb) m_itemCb(item);
                emitProgress(fullPath, static_cast<int>(depth));
                continue;
            }

            // Sockets, FIFOs, devices: ignore.
        }

        ::closedir(dp);
        return totals;
    }

    void retainTopFile(const FileItem& item) {
        const std::size_t cap = m_opt.maxRetainedFiles;
        if (cap == 0) return;
        if (m_topFiles.size() < cap) {
            m_topFiles.push(item);
        } else if (item.allocatedSize > m_topFiles.top().allocatedSize) {
            m_topFiles.pop();
            m_topFiles.push(item);
        }
    }

    const ScanOptions& m_opt;
    ScanResult& m_res;
    Scanner* m_owner;

    std::uintmax_t m_rootDev;
    ConfidenceLevel m_confidence;
    std::vector<std::filesystem::path> m_exclude;
    std::unordered_set<InodeKey, InodeKeyHash> m_seenInodes;
    std::priority_queue<FileItem, std::vector<FileItem>, LargerOnTop> m_topFiles;

    // Callbacks are bound lazily so ScanContext stays cheap to move.
    std::function<void(const ScanProgress&)> m_progressCb;
    std::function<void(const FileItem&)> m_itemCb;
    std::function<void(const DirectorySummary&)> m_dirCb;
    std::uintmax_t m_progressCounter = 0;

public:
    // Wire callbacks from the owning Scanner.
    void bind(std::function<void(const ScanProgress&)> p,
              std::function<void(const FileItem&)> i,
              std::function<void(const DirectorySummary&)> d) {
        m_progressCb = std::move(p);
        m_itemCb = std::move(i);
        m_dirCb = std::move(d);
    }
};

} // namespace

ScanResult Scanner::scan() {
    m_cancelled.store(false, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();

    ScanResult result;
    ScanContext ctx(m_options, result, this);
    ctx.bind(m_progressCb, m_itemCb, m_dirCb);
    ctx.run();

    const auto end = std::chrono::steady_clock::now();
    result.stats.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    result.completed = !result.cancelled && result.error.empty();
    return result;
}

} // namespace driverse::core
