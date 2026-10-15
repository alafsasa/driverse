#include "recommendations.h"
#include "protections.h"
#include "utils.h"

#include <algorithm>
#include <string>

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

bool pathEndsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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
    std::string s = std::filesystem::absolute(home).lexically_normal().string();
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
}

std::optional<CleanupCategory> RecommendationEngine::matchCategory(
    const std::filesystem::path& path, bool isDirectory) const {

    const std::string abs = std::filesystem::absolute(path).lexically_normal().string();
    const std::string home = normalizeHome(m_opts.homeDirectory);
    const RelInfo info = relativeToHome(abs, home);

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
        if (pathStartsWith(rel, "Downloads") || pathStartsWith(rel, "downloads")) {
            return CleanupCategory::Downloads;
        }
        if (pathEndsWith(rel, ".log")) {
            return CleanupCategory::LogFile;
        }
        // Generic application cache — must come after the specific rules above.
        if (pathStartsWith(rel, ".cache/")) {
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
    // nested subdirectories.
    std::vector<DirectorySummary> dirs = scan.directories;
    std::stable_sort(dirs.begin(), dirs.end(),
                     [](const DirectorySummary& a, const DirectorySummary& b) {
                         return a.depth < b.depth;
                     });

    std::vector<std::filesystem::path> acceptedDirs;
    auto underAcceptedDir = [&acceptedDirs](const std::filesystem::path& p) {
        for (const auto& d : acceptedDirs) {
            auto rel = std::filesystem::relative(p, d);
            if (!rel.empty() && *rel.begin() != "..") return true;
        }
        return false;
    };

    for (const auto& d : dirs) {
        if (underAcceptedDir(d.path)) continue;
        auto cand = classifyDirectory(d);
        if (!cand || cand->risk == RiskLevel::SystemProtected) continue;
        acceptedDirs.push_back(cand->path);
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
