#include "integration.h"

#include <algorithm>

namespace driverse::utils {

core::RecommendationOptions recommendationOptionsFromConfig(
    const Config& config,
    const std::filesystem::path& home,
    std::chrono::system_clock::time_point now) {

    core::RecommendationOptions opts;
    opts.homeDirectory = home;
    opts.now = now;
    opts.minAgeForSafe =
        std::chrono::hours(24 * config.getInt("general.min_age_safe_days", 7));
    opts.minAgeForTemp =
        std::chrono::hours(24 * config.getInt("general.min_age_temp_days", 3));
    opts.minAgeForDownloads =
        std::chrono::hours(24 * config.getInt("general.min_age_downloads_days", 90));
    opts.allowPermanentDelete = config.getBool("general.allow_permanent_delete", true);
    return opts;
}

core::ScanOptions scanOptionsFromConfig(const Config& config,
                                        const std::filesystem::path& root) {
    core::ScanOptions opts;
    opts.root = root;
    opts.excludePaths = config.excludedPaths();
    return opts;
}

std::size_t applyProtectedPaths(const Config& config, core::Protections& protections) {
    // Snapshot: addProtectedPath may reallocate the registry's internal vector.
    std::vector<std::filesystem::path> known = protections.getProtectedPaths();
    std::size_t added = 0;
    for (const auto& path : config.protectedPaths()) {
        const auto normalized = path.lexically_normal();
        const bool present =
            std::any_of(known.begin(), known.end(),
                        [&](const std::filesystem::path& p) {
                            return p.lexically_normal() == normalized;
                        });
        if (!present) {
            protections.addProtectedPath(path);
            known.push_back(normalized);
            ++added;
        }
    }
    return added;
}

} // namespace driverse::utils
