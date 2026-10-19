#include "utils/integration.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace driverse::utils;
using namespace std::chrono;
namespace fs = std::filesystem;

namespace {

void testRecommendationOptionsFromConfig() {
    Config c = Config::defaults();
    c.setInt("general.min_age_safe_days", 14);
    c.setInt("general.min_age_temp_days", 5);
    c.setInt("general.min_age_downloads_days", 120);
    c.setBool("general.allow_permanent_delete", false);

    const fs::path home = "/home/alice";
    const auto now = system_clock::now();
    auto opts = recommendationOptionsFromConfig(c, home, now);

    assert(opts.homeDirectory == home);
    assert(opts.now == now);
    assert(opts.minAgeForSafe == hours(24 * 14));
    assert(opts.minAgeForTemp == hours(24 * 5));
    assert(opts.minAgeForDownloads == hours(24 * 120));
    assert(opts.allowPermanentDelete == false);
    std::cout << "  recommendationOptionsFromConfig OK\n";
}

void testScanOptionsFromConfig() {
    Config c = Config::defaults();
    c.setStringList("exclude.paths", {"/proc", "/sys"});
    auto opts = scanOptionsFromConfig(c, "/home/alice");
    assert(opts.root == "/home/alice");
    assert(opts.excludePaths.size() == 2);
    assert(opts.oneFilesystem == true);   // conservative default preserved
    assert(opts.skipProtectedPaths == true);
    assert(!opts.followSymlinks);
    std::cout << "  scanOptionsFromConfig OK\n";
}

void testApplyProtectedPathsIdempotent() {
    Config c = Config::defaults();
    // Add a custom protected path on top of defaults.
    std::vector<std::string> prot;
    for (const auto& p : c.protectedPaths()) prot.push_back(p.string());
    prot.push_back("/opt/important");
    c.setStringList("protected.paths", prot);

    auto& protections = driverse::core::Protections::instance();
    const auto before = protections.getProtectedPaths().size();

    std::size_t added = applyProtectedPaths(c, protections);
    assert(added == 1);
    assert(protections.isProtected("/opt/important/data"));   // now protected
    assert(protections.getProtectedPaths().size() == before + 1);

    // Applying again is idempotent.
    std::size_t addedAgain = applyProtectedPaths(c, protections);
    assert(addedAgain == 0);
    assert(protections.getProtectedPaths().size() == before + 1);

    // Defaults still protected.
    assert(protections.isProtected("/etc/passwd"));
    std::cout << "  applyProtectedPaths idempotent OK\n";
}

void testProtectedPathTrailingSlash() {
    auto& protections = driverse::core::Protections::instance();

    // Config-supplied protected paths often come with a trailing slash
    // ("/srv/data/"). The normalized cache must strip it, otherwise the
    // boundary-exact prefix test misses everything below the entry.
    protections.addProtectedPath("/opt/driverse_trailing/");
    assert(protections.isProtected("/opt/driverse_trailing"));
    assert(protections.isProtected("/opt/driverse_trailing/child"));
    assert(protections.isProtected("/opt/driverse_trailing/child/file.txt"));
    // Component boundary still holds: the sibling prefix is not protected.
    assert(!protections.isProtected("/opt/driverse_trailing2"));

    protections.removeProtectedPath("/opt/driverse_trailing/");
    assert(!protections.isProtected("/opt/driverse_trailing/child"));
    std::cout << "  protected path trailing slash OK\n";
}

} // namespace

int main() {
    std::cout << "Running integration tests...\n";
    testRecommendationOptionsFromConfig();
    testScanOptionsFromConfig();
    testApplyProtectedPathsIdempotent();
    testProtectedPathTrailingSlash();
    std::cout << "All integration tests passed!\n";
    return 0;
}
