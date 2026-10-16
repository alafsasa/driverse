// End-to-end safety scenarios: scan a fake home directory containing caches,
// user data, hard links, a sparse file and a symlink pointing outside the tree;
// run the recommendation engine, then the cleanup pipeline (dry run first, then
// guarded apply). Every assertion here is a safety property, not an
// implementation detail.

#include "core/cleanup.h"
#include "core/protections.h"
#include "core/recommendations.h"
#include "core/scanner.h"
#include "core/utils.h"

#include <unistd.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace driverse::core;

namespace {

fs::path makeBaseDir() {
    std::error_code ec;
    const fs::path dir =
        fs::temp_directory_path(ec) / ("driverse-safety-test-" + std::to_string(::getpid()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    assert(!ec);
    return dir;
}

void writeFile(const fs::path& path, std::size_t bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << std::string(bytes, 'x');
}

void writeSparseFile(const fs::path& path, std::uintmax_t logicalSize) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.seekp(static_cast<std::streamoff>(logicalSize - 1));
    out.put('\0');
}

void ageBy(const fs::path& path, std::chrono::hours age) {
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now() - age, ec);
    assert(!ec);
}

const FileItem* findItem(const ScanResult& result, const std::string& filename) {
    for (const auto& item : result.largestFiles) {
        if (item.path.filename().string() == filename) {
            return &item;
        }
    }
    return nullptr;
}

bool anyPathContains(const ScanResult& result, const std::string& needle) {
    for (const auto& item : result.largestFiles) {
        if (item.path.string().find(needle) != std::string::npos) {
            return true;
        }
    }
    for (const auto& dir : result.directories) {
        if (dir.path.string().find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const CleanupCandidate* findCandidateIn(const std::vector<CleanupCandidate>& candidates,
                                        const std::string& needle) {
    for (const auto& candidate : candidates) {
        if (candidate.path.string().find(needle) != std::string::npos) {
            return &candidate;
        }
    }
    return nullptr;
}

void testScanNeverFollowsSymlinksOutsideRoot(const fs::path& base, const fs::path& home,
                                             const ScanResult& scan) {
    // The tree contains a symlink to `base/outside` (a sibling of the scan
    // root). Nothing beyond the symlink may appear in the results, and the
    // symlink itself is counted as skipped, not followed.
    assert(fs::is_symlink(home / "outside-link"));
    assert(!anyPathContains(scan, "outside"));
    assert(scan.stats.symlinksSkipped >= 1);
    assert(fs::exists(base / "outside" / "secret.txt"));
}

} // namespace

int main() {
    std::cout << "Running safety scenario integration tests...\n";

    const fs::path base = makeBaseDir();
    const fs::path home = base / "home";
    const fs::path trashRoot = base / "Trash";

    // ---- Build a representative home tree ----
    fs::create_directories(home);

    // Regenerable thumbnail cache, old enough to qualify as Safe.
    const fs::path t1 = home / ".cache" / "thumbnails" / "t1.jpg";
    const fs::path t2 = home / ".cache" / "thumbnails" / "t2.jpg";
    writeFile(t1, 20000);
    writeFile(t2, 10000);
    ageBy(t1, std::chrono::hours(24 * 30));
    ageBy(t2, std::chrono::hours(24 * 30));
    ageBy(home / ".cache" / "thumbnails", std::chrono::hours(24 * 30));

    // A generic application cache (never Safe without the right signals).
    writeFile(home / ".cache" / "appdata" / "blob.bin", 5000);

    // User data that must never be classified Safe.
    const fs::path download = home / "Downloads" / "archive.zip";
    writeFile(download, 30000);
    ageBy(download, std::chrono::hours(24 * 100));
    ageBy(home / "Downloads", std::chrono::hours(24 * 100));

    // Hard-linked pair (must be deduplicated) and a sparse file.
    const fs::path big = home / "data" / "big.dat";
    writeFile(big, 40000);
    fs::create_hard_link(big, home / "data" / "big-link.dat");
    const fs::path sparse = home / "data" / "sparse.dat";
    writeSparseFile(sparse, 8ULL * 1024 * 1024);

    // A symlink to a sibling directory: traversal must never follow or count it.
    const fs::path outside = base / "outside";
    writeFile(outside / "secret.txt", 65536);
    fs::create_symlink(outside, home / "outside-link");

    // ---- Scan ----
    ScanOptions scanOptions;
    scanOptions.root = home;
    scanOptions.oneFilesystem = true;
    scanOptions.skipProtectedPaths = true;
    scanOptions.measureAllocatedSize = true;

    Scanner scanner;
    scanner.setOptions(scanOptions);
    const ScanResult scan = scanner.scan();

    assert(scan.error.empty());
    assert(scan.completed);
    assert(!scan.cancelled);
    testScanNeverFollowsSymlinksOutsideRoot(base, home, scan);

    // Hard links are counted once for allocation purposes.
    assert(scan.stats.hardLinksDeduplicated >= 1);
    const FileItem* hard = findItem(scan, "big.dat");
    assert(hard != nullptr);
    assert(hard->isHardLinked);

    // Sparse detection: allocated must be far below logical.
    const FileItem* sparseItem = findItem(scan, "sparse.dat");
    assert(sparseItem != nullptr);
    assert(sparseItem->isSparse);
    assert(sparseItem->logicalSize >= 8ULL * 1024 * 1024);
    assert(sparseItem->allocatedSize < sparseItem->logicalSize);

    // ---- Recommendations ----
    RecommendationOptions recOptions;
    recOptions.homeDirectory = home;
    recOptions.now = std::chrono::system_clock::now();
    RecommendationEngine engine(recOptions);
    const std::vector<CleanupCandidate> candidates = engine.recommend(scan);

    const CleanupCandidate* thumb =
        findCandidateIn(candidates, ".cache/thumbnails");
    assert(thumb != nullptr);
    assert(thumb->risk == RiskLevel::Safe);
    assert(thumb->permanentDeleteAllowed);

    // Downloads are user data: surfaced for review, never Safe, never
    // permanently deletable.
    const CleanupCandidate* downloads = findCandidateIn(candidates, "Downloads");
    assert(downloads != nullptr);
    assert(downloads->risk == RiskLevel::High);
    assert(!downloads->permanentDeleteAllowed);

    assert(totalRecoverable(candidates) > 0);

    // ---- Cleanup pipeline ----
    CleanupOptions cleanupOptions;
    cleanupOptions.mode = CleanupMode::DryRun;
    cleanupOptions.trashRoot = trashRoot;
    cleanupOptions.currentUserUid = static_cast<std::uintmax_t>(::getuid());
    cleanupOptions.homeDirectory = home;
    cleanupOptions.allowPermanentDelete = false;

    const CleanupPlan plan = buildCleanupPlan(candidates, cleanupOptions);
    assert(!plan.items.empty());
    assert(plan.actionableCount >= 1);
    assert(plan.totalBytes > 0);

    // Dry run: nothing on disk may change.
    const CleanupReport dryRun = executeCleanup(plan, cleanupOptions);
    assert(!dryRun.aborted);
    assert(dryRun.moved == 0);
    assert(dryRun.deleted == 0);
    assert(dryRun.dryRun == plan.actionableCount);
    assert(fs::exists(t1));
    assert(fs::exists(download));
    assert(!fs::exists(trashRoot / "files" / "t1.jpg"));

    // ---- Protected paths are blocked at plan time ----
    {
        const fs::path protectedDir = home / "data";
        const fs::path canonicalDir = driverse::core::utils::canonicalizeNoFollow(protectedDir);
        Protections::instance().addProtectedPath(canonicalDir);

        std::vector<CleanupCandidate> guarded;
        for (const char* name : {"big.dat", "sparse.dat"}) {
            CleanupCandidate candidate;
            candidate.path = protectedDir / name;
            candidate.risk = RiskLevel::Low;
            guarded.push_back(candidate);
        }
        // SystemProtected is refused even without a registry entry.
        CleanupCandidate systemCandidate;
        systemCandidate.path = home / "Downloads" / "archive.zip";
        systemCandidate.risk = RiskLevel::SystemProtected;
        guarded.push_back(systemCandidate);

        const CleanupPlan guardedPlan = buildCleanupPlan(guarded, cleanupOptions);
        Protections::instance().removeProtectedPath(canonicalDir);

        assert(guardedPlan.actionableCount == 0);
        assert(guardedPlan.skippedCount == 3);
        assert(guardedPlan.items[0].skipReason == "protected path");
        assert(guardedPlan.items[1].skipReason == "protected path");
        assert(guardedPlan.items[2].skipReason == "system-protected");
        assert(fs::exists(big));
        assert(fs::exists(sparse));
    }

    // ---- Apply: user caches go to the trash, recoverably ----
    {
        CleanupOptions applyOptions = cleanupOptions;
        applyOptions.mode = CleanupMode::Apply;

        std::vector<CleanupCandidate> toTrash;
        CleanupCandidate candidate;
        candidate.path = home / ".cache" / "appdata" / "blob.bin";
        candidate.risk = RiskLevel::Low;
        candidate.allocatedSize = 5000;
        toTrash.push_back(candidate);

        const CleanupPlan trashPlan = buildCleanupPlan(toTrash, applyOptions);
        assert(trashPlan.items[0].action == CleanupAction::MoveToTrash);

        const CleanupReport report = executeCleanup(trashPlan, applyOptions);
        assert(report.ok());
        assert(report.moved == 1);
        assert(!fs::exists(candidate.path));
        assert(fs::exists(trashRoot / "files" / "blob.bin"));
        assert(fs::exists(trashRoot / "info" / "blob.bin.trashinfo"));
    }

    // ---- Apply: opted-in permanent delete only for Safe regenerable caches ----
    {
        CleanupOptions applyOptions = cleanupOptions;
        applyOptions.mode = CleanupMode::Apply;
        applyOptions.allowPermanentDelete = true;

        const fs::path cacheFile = home / ".cache" / "appthing" / "c.bin";
        writeFile(cacheFile, 4096);
        ageBy(cacheFile, std::chrono::hours(24 * 30));

        CleanupCandidate candidate;
        candidate.path = cacheFile;
        candidate.risk = RiskLevel::Safe;
        candidate.permanentDeleteAllowed = true;
        candidate.allocatedSize = 4096;

        const CleanupPlan deletePlan = buildCleanupPlan({candidate}, applyOptions);
        assert(deletePlan.items[0].action == CleanupAction::PermanentDelete);

        const CleanupReport report = executeCleanup(deletePlan, applyOptions);
        assert(report.ok());
        assert(report.deleted == 1);
        assert(!fs::exists(cacheFile));
        assert(!fs::exists(trashRoot / "files" / "c.bin"));
    }

    // ---- TOCTOU: a substituted file aborts the guarded batch ----
    {
        CleanupOptions applyOptions = cleanupOptions;
        applyOptions.mode = CleanupMode::Apply;

        const fs::path target = home / ".cache" / "appdata" / "sub.bin";
        writeFile(target, 2048);

        CleanupCandidate candidate;
        candidate.path = target;
        candidate.risk = RiskLevel::Low;
        candidate.allocatedSize = 2048;

        const CleanupPlan toctouPlan = buildCleanupPlan({candidate}, applyOptions);
        assert(toctouPlan.actionableCount == 1);

        // Replace the file: same path, new inode.
        fs::remove(target);
        writeFile(target, 2048);

        const CleanupReport report = executeCleanup(toctouPlan, applyOptions);
        assert(report.aborted);
        assert(report.moved == 0);
        assert(report.deleted == 0);
        assert(fs::exists(target)); // the substitute was never touched
    }

    // The protected-looking user data survived every scenario.
    assert(fs::exists(big));
    assert(fs::exists(sparse));
    assert(fs::exists(download));

    fs::remove_all(base);
    std::cout << "All safety scenario tests passed!\n";
    return 0;
}
