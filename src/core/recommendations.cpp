#include "recommendations.h"
#include "protections.h"
#include "utils.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <unistd.h>

namespace driverse::core {

namespace {

// Prefix match at path-component boundaries (so ".cache" does not match
// ".cache2" and "/etc" does not match "/etcetera").
bool pathStartsWith(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    if (s.compare(0, prefix.size(), prefix) != 0) return false;
    return s.size() == prefix.size() || s[prefix.size()] == '/';
}

// "~/.cache" relative to home *with* its trailing separator. For matches
// that must be strictly below ~/.cache (pathStartsWith() is unusable for
// this: its boundary check requires the char after the prefix to be '/').
constexpr std::string_view kCacheDirPrefix = ".cache/";

bool pathEndsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Flatpak per-app cache: ~/.var/app/<app-id>/cache[/...]. The sibling
// data/ and config/ directories hold user state and are never candidates.
bool isFlatpakAppCache(const std::string& rel) {
    constexpr std::string_view kFlatpakRoot = ".var/app/";
    if (!rel.starts_with(kFlatpakRoot)) return false;
    std::string_view rest(rel.data() + kFlatpakRoot.size(),
                          rel.size() - kFlatpakRoot.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos) return false; // the <app-id> dir itself
    std::string_view tail = rest.substr(slash + 1);
    return tail == "cache" || tail.starts_with("cache/");
}

// Snap: any directory literally named ".cache" below ~/snap/<name>/ — that is
// the XDG cache a snap sees (revision home or the shared common/ home).
// Other snap dirs (.config, data, common/) are app state, never candidates.
bool isSnapCache(const std::string& rel) {
    if (!rel.starts_with("snap/")) return false;
    std::size_t pos = 0;
    while (pos <= rel.size()) {
        const std::size_t next = rel.find('/', pos);
        const std::string_view comp = (next == std::string::npos)
                                          ? std::string_view(rel).substr(pos)
                                          : std::string_view(rel).substr(pos, next - pos);
        if (comp == ".cache") return true;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return false;
}

struct RelInfo {
    bool inHome = false;
    std::string rel; // relative to home when inHome, else full abs path
};

RelInfo relativeToHome(const std::string& abs, const std::string& home) {
    if (home.empty()) {
        return {false, abs};
    }
    if (abs.size() >= home.size() &&
        abs.compare(0, home.size(), home) == 0 &&
        (abs.size() == home.size() || abs[home.size()] == '/')) {
        std::string r = abs.substr(home.size());
        while (!r.empty() && r.front() == '/') r.erase(0, 1);
        return {true, r};
    }
    return {false, abs};
}

// Canonical project/build artifact names. These are NEVER auto-Safe: deleting
// someone's build output is a user decision, so they map to Medium at best.
bool isBuildArtifactName(const std::string& name) {
    if (name == "build" || name == "target" || name == "CMakeFiles" ||
        name == "CMakeCache.txt" || name == "node_modules") {
        return true;
    }
    if (pathStartsWith(name, "cmake-build-")) return true;

    const std::string ext = std::filesystem::path(name).extension().string();
    return ext == ".o" || ext == ".a";
}

std::string normalizeHome(const std::filesystem::path& home) {
    if (home.empty()) return {};
    std::string s = utils::normalizedAbsoluteString(home);
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

const char* riskColorTag(RiskLevel risk) {
    switch (risk) {
        case RiskLevel::Safe: return "safe";
        case RiskLevel::Low: return "low";
        case RiskLevel::Medium: return "medium";
        case RiskLevel::High: return "high";
        case RiskLevel::SystemProtected: return "system-protected";
    }
    return "unknown";
}

} // namespace

std::string categoryToString(CleanupCategory category) {
    switch (category) {
        case CleanupCategory::Unknown: return "Unknown";
        case CleanupCategory::ThumbnailCache: return "Thumbnail cache";
        case CleanupCategory::BrowserCache: return "Browser cache";
        case CleanupCategory::PackageCache: return "Package cache";
        case CleanupCategory::TempCache: return "Temporary files";
        case CleanupCategory::BuildArtifact: return "Build artifact";
        case CleanupCategory::ApplicationCache: return "Application cache";
        case CleanupCategory::LogFile: return "Log file";
        case CleanupCategory::Downloads: return "Downloaded file";
        case CleanupCategory::SystemCache: return "System cache";
    }
    return "Unknown";
}

std::uintmax_t totalRecoverable(const std::vector<CleanupCandidate>& candidates) {
    std::uintmax_t total = 0;
    for (const auto& c : candidates) {
        total += c.allocatedSize; // already conservative
    }
    return total;
}

std::uintmax_t totalRecoverableForRisk(const std::vector<CleanupCandidate>& candidates,
                                       RiskLevel risk) {
    std::uintmax_t total = 0;
    for (const auto& c : candidates) {
        if (c.risk == risk) total += c.allocatedSize;
    }
    return total;
}

RecommendationEngine::RecommendationEngine(RecommendationOptions opts)
    : m_opts(std::move(opts)) {
    if (m_opts.now.time_since_epoch().count() == 0) {
        m_opts.now = std::chrono::system_clock::now();
    }
    if (m_opts.currentUserUid == 0) {
        m_opts.currentUserUid = static_cast<std::uintmax_t>(::getuid());
    }
    m_homeNormalized = normalizeHome(m_opts.homeDirectory);
}

std::optional<CleanupCategory> RecommendationEngine::matchCategory(
    const std::filesystem::path& path, bool isDirectory) const {

    const std::string abs = utils::normalizedAbsoluteString(path);
    const RelInfo info = relativeToHome(abs, m_homeNormalized);

    if (info.inHome) {
        const std::string& rel = info.rel;

        if (pathStartsWith(rel, ".cache/thumbnails") || pathStartsWith(rel, ".thumbnails")) {
            return CleanupCategory::ThumbnailCache;
        }
        if (pathStartsWith(rel, ".cache/mozilla") || pathStartsWith(rel, ".cache/firefox") ||
            pathStartsWith(rel, ".config/mozilla") ||
            pathStartsWith(rel, ".config/google-chrome") ||
            pathStartsWith(rel, ".config/chromium") || pathStartsWith(rel, ".cache/chromium") ||
            pathStartsWith(rel, ".cache/BraveSoftware")) {
            return CleanupCategory::BrowserCache;
        }
        if (pathStartsWith(rel, ".cache/pacman") || pathStartsWith(rel, ".cache/apk")) {
            return CleanupCategory::PackageCache;
        }
        if (pathStartsWith(rel, ".cache/tmp")) {
            return CleanupCategory::TempCache;
        }
        // Explicit Flatpak/Snap cache detection (G5): sandboxed app caches are
        // regenerable like any other app cache; their data/config state is not
        // and falls through to the conservative "not a candidate" default.
        if (isFlatpakAppCache(rel) || isSnapCache(rel)) {
            return CleanupCategory::ApplicationCache;
        }
        if (pathStartsWith(rel, "Downloads") || pathStartsWith(rel, "downloads")) {
            return CleanupCategory::Downloads;
        }
        if (pathEndsWith(rel, ".log")) {
            return CleanupCategory::LogFile;
        }
        // Generic application cache — must come after the specific rules
        // above. Match strictly *under* ~/.cache: pathStartsWith() can't be
        // used here (its prefix ends with "/", so the component-boundary
        // check could never succeed), and the "~/.cache" directory itself
        // must stay a non-candidate — otherwise it would swallow every
        // per-app cache beneath it in recommend()'s shallowest-first pass.
        if (rel.size() > kCacheDirPrefix.size() &&
            rel.compare(0, kCacheDirPrefix.size(), kCacheDirPrefix) == 0) {
            return CleanupCategory::ApplicationCache;
        }
        if (isBuildArtifactName(std::filesystem::path(abs).filename().string())) {
            return CleanupCategory::BuildArtifact;
        }
        // Anything else under the home directory is unknown user data:
        // the conservative default is "not a candidate".
        (void)isDirectory;
        return std::nullopt;
    }

    // System-wide paths.
    if (pathStartsWith(abs, "/var/cache/apt/archives")) {
        return CleanupCategory::SystemCache;
    }
    if (pathStartsWith(abs, "/tmp") || pathStartsWith(abs, "/var/tmp")) {
        return CleanupCategory::TempCache;
    }
    if (isBuildArtifactName(std::filesystem::path(abs).filename().string())) {
        return CleanupCategory::BuildArtifact;
    }
    return std::nullopt;
}

std::optional<CleanupCandidate> RecommendationEngine::classifyCommon(
    const std::filesystem::path& path,
    std::uintmax_t logicalSize,
    std::uintmax_t allocatedSize,
    std::uintmax_t ownerUid,
    bool isDirectory,
    bool isHardLinked,
    int hardLinkCount,
    std::chrono::system_clock::time_point mtime,
    ConfidenceLevel sizeConfidence) const {

    CleanupCandidate c;
    c.path = path;
    c.logicalSize = logicalSize;
    c.ownerUid = ownerUid;
    c.isDirectory = isDirectory;
    c.mtime = mtime;
    c.isUserOwned = (ownerUid == m_opts.currentUserUid);

    // Hard constraint: protected system paths are never deletable and never
    // contribute to the recoverable estimate.
    if (Protections::instance().isProtected(path)) {
        c.category = CleanupCategory::SystemCache;
        c.risk = RiskLevel::SystemProtected;
        c.confidence = ConfidenceLevel::High;
        c.allocatedSize = 0;
        c.permanentDeleteAllowed = false;
        c.justification = "Protected system path — excluded from cleanup";
        c.tags = {"system", "protected", riskColorTag(RiskLevel::SystemProtected)};
        return c;
    }

    const auto cat = matchCategory(path, isDirectory);
    if (!cat) {
        // Conservative default: unknown items are never candidates.
        return std::nullopt;
    }
    c.category = *cat;

    // Age relative to the configured "now".
    std::int64_t ageHours = std::chrono::duration_cast<std::chrono::hours>(
                                m_opts.now - mtime).count();
    if (ageHours < 0) ageHours = 0;

    // Recent downloads are not surfaced as candidates at all.
    if (c.category == CleanupCategory::Downloads &&
        ageHours < m_opts.minAgeForDownloads.count()) {
        return std::nullopt;
    }

    RiskLevel risk;
    bool basePermanent;
    std::string justification;

    switch (c.category) {
        case CleanupCategory::ThumbnailCache:
            // Thumbnails are genuinely regenerable; require age to avoid
            // thrashing thumbnails for images being actively viewed.
            risk = (ageHours >= m_opts.minAgeForSafe.count()) ? RiskLevel::Safe
                                                              : RiskLevel::Low;
            basePermanent = true;
            justification = "Regenerable thumbnail cache";
            break;
        case CleanupCategory::BrowserCache:
            risk = RiskLevel::Low;
            basePermanent = false;
            justification = "Browser cache (regenerable; may be in use by a running browser)";
            break;
        case CleanupCategory::PackageCache:
            risk = RiskLevel::Low;
            basePermanent = true;
            justification = "Downloaded package cache (package manager can re-fetch)";
            break;
        case CleanupCategory::TempCache:
            risk = (ageHours >= m_opts.minAgeForTemp.count()) ? RiskLevel::Medium
                                                              : RiskLevel::Low;
            basePermanent = true;
            justification = "Temporary/cache directory (may contain active sockets/locks)";
            break;
        case CleanupCategory::BuildArtifact:
            // Name alone is never enough: never below Medium, never permanent.
            risk = RiskLevel::Medium;
            basePermanent = false;
            justification = "Build artifact — project output, review before deleting";
            break;
        case CleanupCategory::ApplicationCache:
            risk = (ageHours >= m_opts.minAgeForTemp.count()) ? RiskLevel::Low
                                                              : RiskLevel::Medium;
            basePermanent = false;
            justification = "Application cache (regenerable; review contents)";
            break;
        case CleanupCategory::LogFile:
            risk = RiskLevel::Low;
            basePermanent = false;
            justification = "Log file";
            break;
        case CleanupCategory::Downloads:
            risk = RiskLevel::High;
            basePermanent = false;
            justification = "Downloaded file — user data, review before deleting";
            break;
        case CleanupCategory::SystemCache:
            risk = RiskLevel::High;
            basePermanent = false;
            justification = "System package cache — delegate to the package manager";
            break;
        case CleanupCategory::Unknown:
        default:
            risk = RiskLevel::Medium;
            basePermanent = false;
            justification = "Unclassified";
            break;
    }

    // Ownership safety rule: not-owned-by-user items are never Safe or Low.
    if (!c.isUserOwned) {
        if (risk == RiskLevel::Safe || risk == RiskLevel::Low) {
            risk = RiskLevel::High;
        }
        justification += "; owned by another user/root";
    }

    // Sensitive-location rule (G4): a category may never whitewash where the
    // file lives. Credential material is floored at High (strong confirmation
    // gate, never permanent-delete eligible), personal state at Medium. The
    // cache exemption in Protections keeps real caches (browser/Chromium
    // caches, thumbnails) out of this, so ordinary caches keep their risk.
    const SensitiveLevel sens = Protections::instance().sensitiveLevel(path);
    if (sens != SensitiveLevel::None) {
        const RiskLevel floor = (sens == SensitiveLevel::Credential)
                                    ? RiskLevel::High
                                    : RiskLevel::Medium;
        if (static_cast<int>(risk) < static_cast<int>(floor)) {
            risk = floor;
        }
        justification += (sens == SensitiveLevel::Credential)
                             ? "; credential/key material — review carefully"
                             : "; sensitive personal state — review carefully";
    }

    // Conservative recoverable estimate. A hard-linked file may share all of its
    // blocks with other names, so deleting one name could free nothing.
    std::uintmax_t recoverable = allocatedSize;
    if (isHardLinked && hardLinkCount > 1) {
        recoverable = 0;
        justification += "; hard-linked (deleting one name may free nothing)";
    }

    // Confidence: take the weaker of the measured size confidence and the
    // category default.
    const ConfidenceLevel catConfidence =
        (c.category == CleanupCategory::SystemCache ||
         c.category == CleanupCategory::BuildArtifact ||
         c.category == CleanupCategory::Downloads)
            ? ConfidenceLevel::Medium
            : ConfidenceLevel::High;
    c.confidence = std::min(sizeConfidence, catConfidence);

    // Permanent-delete gate: opt-in, user-owned, genuinely regenerable, and old
    // enough. Medium/High/SystemProtected are never eligible.
    const bool ageOk = ageHours >= m_opts.minAgeForSafe.count();
    c.permanentDeleteAllowed = m_opts.allowPermanentDelete && basePermanent &&
                               c.isUserOwned && (risk == RiskLevel::Safe ||
                                                 risk == RiskLevel::Low) &&
                               ageOk;

    c.risk = risk;
    c.allocatedSize = recoverable;
    c.justification = justification;
    c.tags = {categoryToString(c.category), riskColorTag(risk)};
    if (sens != SensitiveLevel::None) {
        c.tags.push_back("sensitive");
    }
    return c;
}

std::optional<CleanupCandidate> RecommendationEngine::classify(const FileItem& item) const {
    return classifyCommon(item.path, item.logicalSize, item.allocatedSize, item.ownerUid,
                          item.isDirectory, item.isHardLinked, item.hardLinkCount,
                          item.mtime, item.sizeConfidence);
}

std::optional<CleanupCandidate> RecommendationEngine::classifyDirectory(
    const DirectorySummary& dir) const {
    return classifyCommon(dir.path, dir.logicalSize, dir.uniqueAllocatedSize, dir.ownerUid,
                          /*isDirectory=*/true, /*isHardLinked=*/false, /*hardLinkCount=*/1,
                          dir.mtime, ConfidenceLevel::High);
}

std::vector<CleanupCandidate> RecommendationEngine::recommend(const ScanResult& scan) const {
    std::vector<CleanupCandidate> out;

    // Directories, shallowest first, so an outer cache dir "absorbs" its
    // nested subdirectories. Iterate by pointer: copying every
    // DirectorySummary (each owns a path) is expensive on scans with millions
    // of directories.
    std::vector<const DirectorySummary*> dirs;
    dirs.reserve(scan.directories.size());
    for (const auto& d : scan.directories) {
        dirs.push_back(&d);
    }
    std::stable_sort(dirs.begin(), dirs.end(),
                     [](const DirectorySummary* a, const DirectorySummary* b) {
                         return a->depth < b->depth;
                     });

    // Nesting checks are purely lexical (component-boundary prefix on
    // normalized paths). Never use std::filesystem::relative() here: libstdc++
    // implements it via canonicalization, which turns into a stat/readlink
    // storm — once per directory per accepted candidate — and makes a large
    // scan appear to stall for minutes right before results appear. All paths
    // come from one traversal of one root, so lexical nesting is exact.
    std::vector<std::string> acceptedDirs; // lexically-normal absolute paths
    auto underAcceptedDir = [&acceptedDirs](const std::filesystem::path& path) {
        const std::string p = utils::normalizedAbsoluteString(path);
        for (const auto& d : acceptedDirs) {
            if (d == "/") {
                if (p.size() > 1) return true; // anything below the root
                continue;
            }
            // Strictly nested: shared prefix ending on a component boundary
            // (so ".cache/tmp" does not swallow ".cache/tmp2").
            if (p.size() <= d.size()) continue;
            if (p.compare(0, d.size(), d) == 0 && p[d.size()] == '/') return true;
        }
        return false;
    };

    for (const DirectorySummary* dp : dirs) {
        if (underAcceptedDir(dp->path)) continue;
        auto cand = classifyDirectory(*dp);
        if (!cand || cand->risk == RiskLevel::SystemProtected) continue;
        acceptedDirs.push_back(utils::normalizedAbsoluteString(cand->path));
        out.push_back(std::move(*cand));
    }

    // Individual files from the scan's top-N, excluding those already covered
    // by a recommended directory.
    for (const auto& f : scan.largestFiles) {
        if (underAcceptedDir(f.path)) continue;
        auto cand = classify(f);
        if (!cand || cand->risk == RiskLevel::SystemProtected) continue;
        // A candidate with a zero conservative estimate contributes nothing but
        // is still worth surfacing for review when risk is not Safe (e.g.
        // hard-linked files). Keep anything with recoverable bytes or that is
        // genuinely actionable.
        if (cand->allocatedSize == 0 &&
            (cand->risk == RiskLevel::Safe || cand->risk == RiskLevel::Low)) {
            continue;
        }
        out.push_back(std::move(*cand));
    }

    // Largest recoverable estimate first.
    std::sort(out.begin(), out.end(), [](const CleanupCandidate& a, const CleanupCandidate& b) {
        return a.allocatedSize > b.allocatedSize;
    });
    return out;
}

} // namespace driverse::core
