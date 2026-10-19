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

// Totals gathered by the bounded pre-count pass (G3).
struct PreCountTotals {
    std::uintmax_t dirs = 0;  // directories the main pass will descend into
    std::uintmax_t files = 0; // regular files the main pass will count
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

        // Seed the recursion-stack set with the root so a bind mount that re-exposes
        // an ancestor (same dev+ino) is detected as a cycle rather than recursed forever.
        m_activeDirs.insert({m_rootDev, static_cast<std::uintmax_t>(rootSt.st_ino)});

        // G3: bounded pre-count pass — walk the tree once without per-entry
        // metadata so progress can report a real percentage and ETA. Discarded
        // when the budget runs out or the pass is cancelled.
        if (m_opt.estimateTotals && !m_owner->isCancelled()) {
            const auto budget =
                m_opt.estimateBudget.count() < 0 ? std::chrono::milliseconds(0)
                                                 : m_opt.estimateBudget;
            m_countDeadline = std::chrono::steady_clock::now() + budget;
            PreCountTotals counted;
            if (preCount(m_opt.root, 0, counted) && !m_owner->isCancelled()) {
                m_estDirs = counted.dirs;
                m_estFiles = counted.files;
            }
            emitCountingDone();
        }

        if (m_owner->isCancelled()) {
            m_res.cancelled = true;
        } else {
            DirTotals totals = scanDirectory(m_opt.root, 0);
            m_res.stats.totalLogicalBytes = totals.logical;
            m_res.stats.totalAllocatedBytes = totals.allocated;
            m_res.stats.uniqueAllocatedBytes = totals.uniqueAllocated;
        }

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
            p.totalDirs = m_estDirs;
            p.totalFiles = m_estFiles;
            m_progressCb(p);
        }
    }

    // Announce that the pre-count phase finished: totals are now known, or 0
    // when the pass was abandoned. Always emitted, never throttled.
    void emitCountingDone() {
        if (!m_progressCb) return;
        ScanProgress p;
        p.currentPath = m_opt.root;
        p.totalDirs = m_estDirs;
        p.totalFiles = m_estFiles;
        m_progressCb(p);
    }

    // Progress while counting (totals still unknown). The first event is
    // always emitted so even tiny scans show the "Preparing" phase.
    void emitCounting(const std::filesystem::path& current, int depth,
                      const PreCountTotals& t) {
        if (!m_progressCb) return;
        const std::uintmax_t n = ++m_countCounter;
        if (n != 1 && n % kProgressInterval != 0) return;
        ScanProgress p;
        p.filesScanned = t.files;
        p.dirsScanned = t.dirs;
        p.currentDepth = depth;
        p.currentPath = current;
        p.counting = true;
        m_progressCb(p);
    }

    // G3: bounded pre-count pass. Mirrors scanDirectory()'s traversal
    // decisions (excludes, filesystem boundary, protection, depth cap, cycle
    // guard) so the totals match what the main pass will actually visit, but
    // only tallies: no per-entry stat unless d_type is unknown, no result
    // mutation (so skips/counts are never recorded twice). Returns false when
    // abandoned — budget spent or cancelled — and the caller discards the
    // partial totals.
    bool preCount(const std::filesystem::path& dir, std::size_t depth,
                  PreCountTotals& t) {
        if (m_owner->isCancelled()) {
            return false;
        }
        if (std::chrono::steady_clock::now() >= m_countDeadline) {
            return false;
        }
        emitCounting(dir, static_cast<int>(depth), t);

        DIR* dp = ::opendir(dir.c_str());
        if (!dp) {
            return true; // the main pass records the skip and counts nothing
        }
        const int dirFd = ::dirfd(dp);
        ++t.dirs; // mirrors ++m_res.stats.dirsScanned after a successful opendir

        bool ok = true;
        struct dirent* entry = nullptr;
        while (ok && (entry = ::readdir(dp)) != nullptr) {
            if (m_owner->isCancelled()) {
                m_res.cancelled = true;
                ok = false;
                break;
            }

            const char* name = entry->d_name;
            if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
                continue;
            }
            const std::filesystem::path fullPath = dir / name;
            if (excluded(fullPath)) {
                continue; // main pass skips these too
            }

            // Cheap typing first: regular files and symlinks/specials need no
            // stat here; directories (and unknown d_type) do, for the
            // filesystem-boundary and cycle checks.
            const unsigned char dtype = entry->d_type;
            if (dtype == DT_REG) {
                ++t.files;
                continue;
            }
            if (dtype == DT_LNK || dtype == DT_FIFO || dtype == DT_SOCK ||
                dtype == DT_BLK || dtype == DT_CHR) {
                continue; // symlinks counted nowhere; special files ignored
            }

            struct stat st {};
            if (::fstatat(dirFd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                continue; // main pass records the skip
            }
            if (S_ISLNK(st.st_mode)) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                if (m_opt.oneFilesystem &&
                    static_cast<std::uintmax_t>(st.st_dev) != m_rootDev) {
                    continue;
                }
                if (m_opt.skipProtectedPaths && fullPath != m_opt.root &&
                    utils::isProtectedPath(fullPath)) {
                    continue;
                }
                if (m_opt.maxDepth > 0 && depth + 1 >= m_opt.maxDepth) {
                    continue; // counted but not descended by the main pass
                }
                const InodeKey dirKey{static_cast<std::uintmax_t>(st.st_dev),
                                      static_cast<std::uintmax_t>(st.st_ino)};
                if (m_activeDirs.find(dirKey) != m_activeDirs.end()) {
                    continue; // cycle (G1): main pass skips it as well
                }
                m_activeDirs.insert(dirKey);
                ok = preCount(fullPath, depth + 1, t);
                m_activeDirs.erase(dirKey);
                continue;
            }
            if (S_ISREG(st.st_mode)) {
                ++t.files; // d_type was DT_UNKNOWN
                continue;
            }
            // Sockets/FIFOs/devices: ignored by the main pass too.
        }

        ::closedir(dp);
        return ok;
    }

    DirTotals scanDirectory(const std::filesystem::path& dir, std::size_t depth) {
        DirTotals totals;

        if (m_owner->isCancelled()) {
            m_res.cancelled = true;
            return totals;
        }

        // Report the directory *before* enumerating it, so that a scan blocked
        // inside a slow or hung directory still shows where it is.
        emitProgress(dir, static_cast<int>(depth));

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

                // Cycle guard (G1): a bind mount of an ancestor reappears with the same
                // (dev, ino) on the recursion stack. Detect it before descending.
                const InodeKey dirKey{static_cast<std::uintmax_t>(st.st_dev),
                                      static_cast<std::uintmax_t>(st.st_ino)};
                if (m_activeDirs.find(dirKey) != m_activeDirs.end()) {
                    recordSkip(fullPath, SkipReason::LoopDetected);
                    continue;
                }
                m_activeDirs.insert(dirKey);

                DirTotals sub = scanDirectory(fullPath, depth + 1);

                m_activeDirs.erase(dirKey);
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
                summary.ownerUid = static_cast<std::uintmax_t>(st.st_uid);
                summary.mtime = std::chrono::system_clock::from_time_t(st.st_mtime);
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
                item.ownerUid = static_cast<std::uintmax_t>(st.st_uid);
                item.mtime = std::chrono::system_clock::from_time_t(st.st_mtime);

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
    // Directories currently on the recursion stack, for bind-mount cycle detection.
    std::unordered_set<InodeKey, InodeKeyHash> m_activeDirs;
    std::priority_queue<FileItem, std::vector<FileItem>, LargerOnTop> m_topFiles;

    // Callbacks are bound lazily so ScanContext stays cheap to move.
    std::function<void(const ScanProgress&)> m_progressCb;
    std::function<void(const FileItem&)> m_itemCb;
    std::function<void(const DirectorySummary&)> m_dirCb;
    std::uintmax_t m_progressCounter = 0;

    // G3 pre-count state: estimates handed out via progress, the deadline for
    // the bounded pass, and its own emission counter.
    std::uintmax_t m_estDirs = 0;
    std::uintmax_t m_estFiles = 0;
    std::chrono::steady_clock::time_point m_countDeadline{};
    std::uintmax_t m_countCounter = 0;

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
