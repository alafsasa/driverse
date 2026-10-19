#include "core/recommendations.h"
#include "core/scanner.h"
#include "core/types.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace driverse::core;
using namespace std::chrono;

namespace {

constexpr std::uintmax_t kUid = 1000;
const std::string kHome = "/home/testuser";
const system_clock::time_point kNow = system_clock::now();

RecommendationEngine makeEngine(std::uintmax_t homeUid = kUid) {
    RecommendationOptions opts;
    opts.homeDirectory = kHome;
    opts.now = kNow;
    opts.currentUserUid = kUid;
    (void)homeUid;
    return RecommendationEngine(opts);
}

FileItem mkFile(const std::string& path, std::uintmax_t size,
                std::uintmax_t uid = kUid, int ageDays = 30, int nlink = 1) {
    FileItem it{};
    it.path = path;
    it.name = std::filesystem::path(path).filename();
    it.logicalSize = size;
    it.allocatedSize = size;
    it.ownerUid = uid;
    it.mtime = kNow - hours(24 * ageDays);
    it.type = FileType::RegularFile;
    it.isDirectory = false;
    it.hardLinkCount = nlink;
    it.isHardLinked = nlink > 1;
    it.sizeConfidence = ConfidenceLevel::High;
    return it;
}

DirectorySummary mkDir(const std::string& path, std::uintmax_t uniqueSize,
                       std::size_t depth, std::uintmax_t uid = kUid, int ageDays = 30) {
    DirectorySummary d{};
    d.path = path;
    d.logicalSize = uniqueSize;
    d.allocatedSize = uniqueSize;
    d.uniqueAllocatedSize = uniqueSize;
    d.ownerUid = uid;
    d.mtime = kNow - hours(24 * ageDays);
    d.depth = depth;
    d.fileCount = 1;
    d.dirCount = 0;
    return d;
}

void testThumbnailOldIsSafe() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile(kHome + "/.cache/thumbnails/normal/abc.png", 2048, kUid, 30));
    assert(c.has_value());
    assert(c->category == CleanupCategory::ThumbnailCache);
    assert(c->risk == RiskLevel::Safe);
    assert(c->permanentDeleteAllowed);
    assert(c->isUserOwned);
    std::cout << "  thumbnail (old, user) -> Safe, permanent OK\n";
}

void testThumbnailRecentIsLowOnly() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile(kHome + "/.cache/thumbnails/normal/new.png", 2048, kUid, 0));
    assert(c.has_value());
    assert(c->risk == RiskLevel::Low);
    assert(!c->permanentDeleteAllowed); // too recent for permanent delete
    std::cout << "  thumbnail (recent) -> Low, no permanent\n";
}

void testBrowserCache() {
    auto engine = makeEngine();
    auto c = engine.classify(
        mkFile(kHome + "/.cache/mozilla/firefox/xyz.default/cache2/entries/AB12", 4096, kUid, 10));
    assert(c.has_value());
    assert(c->category == CleanupCategory::BrowserCache);
    assert(c->risk == RiskLevel::Low);
    assert(!c->permanentDeleteAllowed); // never permanent for browser cache
    std::cout << "  browser cache -> Low, no permanent\n";
}

void testPackageCache() {
    auto engine = makeEngine();
    auto c = engine.classify(
        mkFile(kHome + "/.cache/pacman/pkg/foo-1.0-1-x86_64.pkg.tar.zst", 8192, kUid, 30));
    assert(c.has_value());
    assert(c->category == CleanupCategory::PackageCache);
    assert(c->risk == RiskLevel::Low);
    assert(c->permanentDeleteAllowed);
    std::cout << "  package cache -> Low, permanent OK\n";
}

void testTempCache() {
    auto engine = makeEngine();
    auto old = engine.classify(mkFile("/tmp/oldfile.tmp", 1024, kUid, 30));
    assert(old.has_value());
    assert(old->category == CleanupCategory::TempCache);
    assert(old->risk == RiskLevel::Medium); // temp is never Safe
    assert(!old->permanentDeleteAllowed);   // Medium not eligible

    auto recent = engine.classify(mkFile("/tmp/recent.tmp", 1024, kUid, 0));
    assert(recent.has_value());
    assert(recent->risk == RiskLevel::Low);
    assert(!recent->permanentDeleteAllowed);
    std::cout << "  temp cache -> Medium(old)/Low(new)\n";
}

void testBuildArtifactNeverSafe() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile(kHome + "/project/build", 100000, kUid, 100));
    assert(c.has_value());
    assert(c->category == CleanupCategory::BuildArtifact);
    assert(c->risk == RiskLevel::Medium);
    assert(c->risk != RiskLevel::Safe);
    assert(!c->permanentDeleteAllowed);
    std::cout << "  build artifact -> Medium (never Safe)\n";
}

void testDownloadsAgeGate() {
    auto engine = makeEngine();
    auto old = engine.classify(mkFile(kHome + "/Downloads/ubuntu.iso", 5000000, kUid, 200));
    assert(old.has_value());
    assert(old->category == CleanupCategory::Downloads);
    assert(old->risk == RiskLevel::High);
    assert(!old->permanentDeleteAllowed);

    auto recent = engine.classify(mkFile(kHome + "/Downloads/today.zip", 5000, kUid, 5));
    assert(!recent.has_value()); // too recent -> not a candidate
    std::cout << "  downloads -> High when old, hidden when recent\n";
}

void testSystemCache() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile("/var/cache/apt/archives/foo_1.0_amd64.deb", 4096, 0, 30));
    assert(c.has_value());
    assert(c->category == CleanupCategory::SystemCache);
    assert(c->risk == RiskLevel::High);
    assert(!c->permanentDeleteAllowed);
    std::cout << "  system apt cache -> High, no permanent\n";
}

void testProtectedPath() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile("/etc/shadow", 2048, 0, 500));
    assert(c.has_value());
    assert(c->risk == RiskLevel::SystemProtected);
    assert(c->allocatedSize == 0); // contributes nothing to recoverable space
    assert(!c->permanentDeleteAllowed);
    std::cout << "  protected path -> SystemProtected, 0 recoverable\n";
}

void testUnknownNotCandidate() {
    auto engine = makeEngine();
    assert(!engine.classify(mkFile(kHome + "/Documents/thesis.pdf", 900000, kUid, 300)).has_value());
    // A file merely named "cache" must NOT be auto-recommended (name alone is insufficient).
    assert(!engine.classify(mkFile(kHome + "/notes/cache", 100, kUid, 300)).has_value());
    // Boundary check: ~/.cache2 must not be treated as ~/.cache.
    assert(!engine.classify(mkFile(kHome + "/.cache2/data", 100, kUid, 30)).has_value());
    std::cout << "  unknown / name-only / .cache2 -> not a candidate\n";
}

void testRootOwnedDowngraded() {
    auto engine = makeEngine();
    // Same thumbnail path but owned by root -> cannot be Safe.
    auto c = engine.classify(mkFile(kHome + "/.cache/thumbnails/normal/x.png", 2048, 0, 30));
    assert(c.has_value());
    assert(c->risk == RiskLevel::High);
    assert(!c->isUserOwned);
    assert(!c->permanentDeleteAllowed);
    std::cout << "  root-owned cache -> High (never Safe)\n";
}

void testHardLinkedRecoverableZero() {
    auto engine = makeEngine();
    auto c = engine.classify(mkFile(kHome + "/.cache/pacman/pkg/x.pkg.tar.zst", 4096, kUid, 30, 2));
    assert(c.has_value());
    assert(c->allocatedSize == 0); // conservative: another link may remain
    assert(c->allocatedSize == 0);
    std::cout << "  hard-linked file -> 0 recoverable\n";
}

void testLogFile() {
    auto engine = makeEngine();
    // A log outside the personal-state trees keeps its Low risk...
    auto plain = engine.classify(mkFile(kHome + "/project/app.log", 2048, kUid, 10));
    assert(plain.has_value());
    assert(plain->category == CleanupCategory::LogFile);
    assert(plain->risk == RiskLevel::Low);

    // ...but the same file below ~/.local/share is personal state and is
    // floored at Medium with a warning (G4 sensitive-location rule).
    auto c = engine.classify(mkFile(kHome + "/.local/share/app/app.log", 2048, kUid, 10));
    assert(c.has_value());
    assert(c->category == CleanupCategory::LogFile);
    assert(c->risk == RiskLevel::Medium);
    assert(c->justification.find("sensitive personal state") != std::string::npos);
    std::cout << "  log file -> Low (plain), Medium (personal state)\n";
}

// G4: no category may whitewash where a file lives — credential material
// floors at High, personal state at Medium, and real cache dirs below a
// profile (browser/Chromium caches) stay exempt.
void testSensitiveLocationFloors() {
    auto engine = makeEngine();

    // Low -> High: a log inside ~/.ssh is credential territory.
    auto cred = engine.classify(mkFile(kHome + "/.ssh/session.log", 1024, kUid, 40));
    assert(cred.has_value());
    assert(cred->risk == RiskLevel::High);
    assert(!cred->permanentDeleteAllowed);
    assert(cred->justification.find("credential") != std::string::npos);
    bool tagged = false;
    for (const auto& t : cred->tags) {
        if (t == "sensitive") tagged = true;
    }
    assert(tagged);

    // Medium -> High: a directory that looks like a build artifact but sits
    // in ~/.gnupg.
    auto gpgBuild = engine.classify(mkFile(kHome + "/.gnupg/build", 512, kUid, 40));
    assert(gpgBuild.has_value());
    assert(gpgBuild->category == CleanupCategory::BuildArtifact);
    assert(gpgBuild->risk == RiskLevel::High);

    // Low -> Medium: browser state below ~/.config (not a cache subdir).
    auto profile = engine.classify(mkFile(kHome + "/.config/mozilla/app.log", 512, kUid, 40));
    assert(profile.has_value());
    assert(profile->risk == RiskLevel::Medium);
    assert(profile->justification.find("sensitive personal state") != std::string::npos);

    // Exempt: a real cache directory inside the same profile stays Low.
    auto cacheEntry = engine.classify(
        mkFile(kHome + "/.config/mozilla/xyz.default/cache2/entries/AB12", 4096, kUid, 10));
    assert(cacheEntry.has_value());
    assert(cacheEntry->risk == RiskLevel::Low);

    // Control: caches below ~/.cache are untouched by the rule.
    auto plainCache = engine.classify(
        mkFile(kHome + "/.cache/mozilla/firefox/xyz.default/cache2/entries/AB12", 4096, kUid, 10));
    assert(plainCache.has_value());
    assert(plainCache->risk == RiskLevel::Low);
    std::cout << "  sensitive-location risk floors OK\n";
}

void testRecommendDeduplicatesNestedDirs() {
    auto engine = makeEngine();
    ScanResult scan;
    scan.root = kHome;
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails", 1000, 3));
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails/normal", 800, 4));
    scan.largestFiles.push_back(
        mkFile(kHome + "/.cache/thumbnails/normal/thumb.png", 700, kUid, 30));
    // A separate, non-nested candidate:
    scan.largestFiles.push_back(
        mkFile(kHome + "/.cache/pacman/pkg/thing.pkg.tar.zst", 500, kUid, 30));

    auto out = engine.recommend(scan);

    // Expect: the outer thumbnail dir + the standalone package-cache file.
    assert(out.size() == 2);

    bool sawThumbDir = false;
    bool sawNested = false;
    bool sawPkgFile = false;
    for (const auto& c : out) {
        if (c.path == (kHome + "/.cache/thumbnails")) sawThumbDir = true;
        if (c.path == (kHome + "/.cache/thumbnails/normal")) sawNested = true;
        if (c.path == (kHome + "/.cache/pacman/pkg/thing.pkg.tar.zst")) sawPkgFile = true;
    }
    assert(sawThumbDir);
    assert(!sawNested);   // nested dir absorbed by its parent
    assert(sawPkgFile);   // standalone file kept

    std::uintmax_t total = totalRecoverable(out);
    assert(total == 1000 + 500);
    assert(totalRecoverableForRisk(out, RiskLevel::Safe) == 1000);
    std::cout << "  recommend() dedup + totals OK\n";
}

void testNestingBoundaryIsComponentWise() {
    auto engine = makeEngine();
    ScanResult scan;
    scan.root = kHome;
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails", 1000, 3));
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails/normal", 800, 4));
    // Shares the string prefix with the thumbnails dir but is a different
    // component: it must NOT be absorbed by that candidate.
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails2", 600, 3));

    auto out = engine.recommend(scan);

    bool sawThumb = false;
    bool sawNested = false;
    bool sawThumb2 = false;
    for (const auto& c : out) {
        if (c.path == (kHome + "/.cache/thumbnails")) sawThumb = true;
        if (c.path == (kHome + "/.cache/thumbnails/normal")) sawNested = true;
        if (c.path == (kHome + "/.cache/thumbnails2")) sawThumb2 = true;
    }
    assert(out.size() == 2);
    assert(sawThumb);
    assert(sawThumb2);
    assert(!sawNested);
    std::cout << "  nesting boundary is component-wise OK\n";
}

// The generic rule for app caches must (a) fire for anything strictly under
// ~/.cache and (b) leave "~/.cache" itself alone — if the container dir were
// a candidate it would absorb every per-app cache in recommend()'s
// shallowest-first pass and hide the specific classifications.
void testGenericApplicationCacheClassification() {
    auto engine = makeEngine();
    ScanResult scan;
    scan.root = kHome;
    scan.directories.push_back(mkDir(kHome + "/.cache", 5000, 1));
    scan.directories.push_back(mkDir(kHome + "/.cache/someapp", 3000, 2));
    scan.directories.push_back(mkDir(kHome + "/.cache/someapp/sub", 700, 3));

    auto out = engine.recommend(scan);

    bool sawCacheRoot = false;
    bool sawApp = false;
    bool sawSub = false;
    for (const auto& c : out) {
        if (c.path == (kHome + "/.cache")) sawCacheRoot = true;
        if (c.path == (kHome + "/.cache/someapp")) {
            sawApp = true;
            assert(c.category == CleanupCategory::ApplicationCache);
            assert(c.risk != RiskLevel::Safe); // conservative by design
        }
        if (c.path == (kHome + "/.cache/someapp/sub")) sawSub = true;
    }
    assert(out.size() == 1);
    assert(sawApp);
    assert(!sawCacheRoot);
    assert(!sawSub);
    std::cout << "  generic application cache classification OK\n";
}

// G5: sandboxed app caches must be detected explicitly — Flatpak per-app
// caches below ~/.var/app/<id>/cache, Snap XDG caches inside ~/snap/<name>/.
// Their data/config state must stay out of the candidate list.
void testFlatpakAndSnapCaches() {
    auto engine = makeEngine();
    ScanResult scan;
    scan.root = kHome;
    scan.directories.push_back(mkDir(kHome + "/.var/app/org.test.App", 100, 2));
    scan.directories.push_back(mkDir(kHome + "/.var/app/org.test.App/cache", 900, 3));
    scan.directories.push_back(mkDir(kHome + "/.var/app/org.test.App/cache/nested", 100, 4));
    scan.directories.push_back(mkDir(kHome + "/.var/app/org.test.App/data", 500, 3));
    scan.directories.push_back(mkDir(kHome + "/.var/app/org.test.App/config", 400, 3));
    scan.directories.push_back(mkDir(kHome + "/snap/demo/5/.cache", 800, 3));
    scan.directories.push_back(mkDir(kHome + "/snap/demo/common/.cache", 300, 3));
    scan.directories.push_back(mkDir(kHome + "/snap/demo/5/.config", 700, 3));

    auto out = engine.recommend(scan);

    bool sawFlatpakCache = false;
    bool sawFlatpakNested = false;
    bool sawSnapRevCache = false;
    bool sawSnapCommonCache = false;
    bool sawFlatpakApp = false;
    bool sawFlatpakData = false;
    bool sawFlatpakConfig = false;
    bool sawSnapConfig = false;
    for (const auto& c : out) {
        if (c.path == (kHome + "/.var/app/org.test.App/cache")) {
            sawFlatpakCache = true;
            assert(c.category == CleanupCategory::ApplicationCache);
        }
        if (c.path == (kHome + "/.var/app/org.test.App/cache/nested")) sawFlatpakNested = true;
        if (c.path == (kHome + "/snap/demo/5/.cache")) sawSnapRevCache = true;
        if (c.path == (kHome + "/snap/demo/common/.cache")) sawSnapCommonCache = true;
        if (c.path == (kHome + "/.var/app/org.test.App")) sawFlatpakApp = true;
        if (c.path == (kHome + "/.var/app/org.test.App/data")) sawFlatpakData = true;
        if (c.path == (kHome + "/.var/app/org.test.App/config")) sawFlatpakConfig = true;
        if (c.path == (kHome + "/snap/demo/5/.config")) sawSnapConfig = true;
    }
    assert(out.size() == 3);
    assert(sawFlatpakCache && sawSnapRevCache && sawSnapCommonCache);
    assert(!sawFlatpakNested); // absorbed by its parent cache
    assert(!sawFlatpakApp && !sawFlatpakData && !sawFlatpakConfig && !sawSnapConfig);
    std::cout << "  flatpak/snap cache rules OK\n";
}

// Regression guard: nesting checks used to canonicalize paths per directory
// (a stat/readlink storm per accepted candidate), which made scans with many
// directories appear to hang for minutes right before the results appeared.
// Classification must stay purely in-memory.
void testRecommendLargeScanCompletesQuickly() {
    auto engine = makeEngine();
    ScanResult scan;
    scan.root = kHome;
    scan.directories.push_back(mkDir(kHome + "/.cache/thumbnails", 1000, 3));
    for (int i = 0; i < 50000; ++i) {
        scan.directories.push_back(
            mkDir(kHome + "/project/mod" + std::to_string(i % 100) + "/sub" +
                      std::to_string(i),
                  4096, 5));
    }

    const auto t0 = steady_clock::now();
    auto out = engine.recommend(scan);
    const auto elapsedMs =
        duration_cast<milliseconds>(steady_clock::now() - t0).count();

    assert(out.size() == 1);
    assert(out.front().path == (kHome + "/.cache/thumbnails"));
    // Generous bound (this runs with asserts on, often unoptimized); the
    // canonicalization bug took well over 30x this long.
    assert(elapsedMs < 5000);
    std::cout << "  large-scan recommend() is fast OK (" << elapsedMs << " ms)\n";
}

void writeFile(const std::filesystem::path& p, std::size_t bytes) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    std::string chunk(1024, 'x');
    std::size_t written = 0;
    while (written < bytes) {
        std::size_t n = std::min<std::size_t>(chunk.size(), bytes - written);
        out.write(chunk.data(), static_cast<std::streamsize>(n));
        written += n;
    }
}

void setMtime(const std::filesystem::path& p, int ageDays) {
    timespec ts[2];
    auto t = system_clock::to_time_t(kNow - hours(24 * ageDays));
    ts[0].tv_sec = t;
    ts[0].tv_nsec = 0;
    ts[1] = ts[0];
    ::utimensat(AT_FDCWD, p.c_str(), ts, 0);
}

// Real filesystem: build a fake home, scan it, then classify.
void testEngineIntegrationEndToEnd() {
    namespace fs = std::filesystem;
    fs::path home = fs::temp_directory_path() /
                    ("driverse_rec_home_" + std::to_string(::getpid()));
    fs::remove_all(home);
    fs::create_directories(home);

    writeFile(home / ".cache/thumbnails/normal/a.png", 4096);
    writeFile(home / ".cache/pacman/pkg/x.pkg.tar.zst", 8192);
    writeFile(home / "Downloads/recent.zip", 1024);
    writeFile(home / "Documents/keep.txt", 512);

    setMtime(home / ".cache/thumbnails/normal/a.png", 30);
    setMtime(home / ".cache/pacman/pkg/x.pkg.tar.zst", 30);
    setMtime(home / "Downloads/recent.zip", 1);
    setMtime(home / "Documents/keep.txt", 300);
    // Directory mtimes last, since writing entries updates them.
    setMtime(home / ".cache/thumbnails", 30);
    setMtime(home / ".cache/pacman", 30);

    ScanOptions opt;
    opt.root = home;
    opt.oneFilesystem = true;
    opt.skipProtectedPaths = true;
    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult scan = scanner.scan();
    assert(scan.completed);
    assert(scan.error.empty());

    RecommendationOptions ropts;
    ropts.homeDirectory = home;
    ropts.now = kNow;
    ropts.currentUserUid = static_cast<std::uintmax_t>(::getuid());
    RecommendationEngine engine(ropts);
    auto out = engine.recommend(scan);

    bool sawThumbCache = false;
    bool sawPkg = false;
    bool sawDownloads = false;
    bool sawDocuments = false;
    for (const auto& c : out) {
        if (c.category == CleanupCategory::ThumbnailCache) sawThumbCache = true;
        if (c.category == CleanupCategory::PackageCache) sawPkg = true;
        if (c.category == CleanupCategory::Downloads) sawDownloads = true;
        if (c.path.string().find("/Documents/") != std::string::npos) sawDocuments = true;
        assert(c.risk != RiskLevel::SystemProtected);
    }
    assert(sawThumbCache);
    assert(sawPkg);
    assert(!sawDownloads); // recent download must be hidden
    assert(!sawDocuments); // unknown user data must not be recommended

    fs::remove_all(home);
    std::cout << "  end-to-end scan+recommend OK (candidates=" << out.size() << ")\n";
}

} // namespace

int main() {
    std::cout << "Running recommendation tests...\n";
    testThumbnailOldIsSafe();
    testThumbnailRecentIsLowOnly();
    testBrowserCache();
    testPackageCache();
    testTempCache();
    testBuildArtifactNeverSafe();
    testDownloadsAgeGate();
    testSystemCache();
    testProtectedPath();
    testUnknownNotCandidate();
    testRootOwnedDowngraded();
    testHardLinkedRecoverableZero();
    testLogFile();
    testSensitiveLocationFloors();
    testRecommendDeduplicatesNestedDirs();
    testNestingBoundaryIsComponentWise();
    testGenericApplicationCacheClassification();
    testFlatpakAndSnapCaches();
    testRecommendLargeScanCompletesQuickly();
    testEngineIntegrationEndToEnd();
    std::cout << "All recommendation tests passed!\n";
    return 0;
}
