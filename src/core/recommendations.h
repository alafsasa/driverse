#pragma once

#include "types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace driverse::core {

// Tuning knobs for the recommendation engine. All age thresholds are the
// minimum age required BEFORE an item is eligible for a given risk level or for
// permanent deletion. Conservative defaults.
struct RecommendationOptions {
    // Used to anchor relative paths (e.g. ~/.cache/thumbnails) and ownership.
    std::filesystem::path homeDirectory{"/root"};
    // Optional: the project the user launched from, used to flag build artifacts.
    std::filesystem::path workingDirectory{};
    // Reference "now" for age computations.
    std::chrono::system_clock::time_point now{};
    // Thumbnails/old caches must be at least this old to count as Safe.
    std::chrono::hours minAgeForSafe{std::chrono::hours(24 * 7)};
    // Temp dirs must be this old before we consider them.
    std::chrono::hours minAgeForTemp{std::chrono::hours(24 * 3)};
    // Downloads must be this old before we even surface them as a candidate.
    std::chrono::hours minAgeForDownloads{std::chrono::hours(24 * 90)};
    // Current user's uid — used for the ownership safety rule.
    std::uintmax_t currentUserUid{0};
    // Which categories are permitted to bypass trash (permanent delete).
    // Conservative: only genuinely regenerable caches are ever eligible, and
    // permanentDeleteAllowed is still gated on user ownership + age.
    bool allowPermanentDelete{true};
};

// Classifies filesystem paths into cleanup candidates using conservative
// heuristics. A filename such as "cache" or "tmp" alone is NEVER sufficient:
// matching requires a known cache location PLUS an ownership/age check.
//
// The engine never recommends anything it cannot justify, and protected system
// paths are always SystemProtected with zero recoverable estimate.
class RecommendationEngine {
public:
    explicit RecommendationEngine(RecommendationOptions opts = {});

    const RecommendationOptions& options() const { return m_opts; }

    // Classify a single regular file. Returns nullopt if it is not a
    // recognized cleanup candidate (i.e. unknown — the conservative default).
    std::optional<CleanupCandidate> classify(const FileItem& item) const;

    // Classify a directory (e.g. an entire cache directory) as one candidate,
    // using its hard-link-deduplicated allocated size as the recoverable estimate.
    std::optional<CleanupCandidate> classifyDirectory(const DirectorySummary& dir) const;

    // Build a deduplicated candidate list from a scan result. Directories are
    // considered outermost-first; a file inside an already-recommended cache
    // directory is not listed separately (avoid double counting).
    std::vector<CleanupCandidate> recommend(const ScanResult& scan) const;

private:
    std::optional<CleanupCategory> matchCategory(const std::filesystem::path& path,
                                                 bool isDirectory) const;

    std::optional<CleanupCandidate> classifyCommon(const std::filesystem::path& path,
                                                   std::uintmax_t logicalSize,
                                                   std::uintmax_t allocatedSize,
                                                   std::uintmax_t ownerUid,
                                                   bool isDirectory,
                                                   bool isHardLinked,
                                                   int hardLinkCount,
                                                   std::chrono::system_clock::time_point mtime,
                                                   ConfidenceLevel sizeConfidence) const;

    RecommendationOptions m_opts;
};

// Human-readable category name (for logs / UI).
std::string categoryToString(CleanupCategory category);

// Sum of conservative recoverable estimates across candidates.
std::uintmax_t totalRecoverable(const std::vector<CleanupCandidate>& candidates);

// Sum of conservative recoverable estimates for a single risk level.
std::uintmax_t totalRecoverableForRisk(const std::vector<CleanupCandidate>& candidates,
                                       RiskLevel risk);

} // namespace driverse::core
