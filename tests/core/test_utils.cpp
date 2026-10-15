#include "core/utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <system_error>
#include <unistd.h>

using namespace driverse::core;
namespace fs = std::filesystem;

static void testFormatBytes() {
    assert(utils::formatBytes(0) == "0 B");
    assert(utils::formatBytes(512) == "512 B");
    assert(utils::formatBytes(1024) == "1.0 KiB");
    assert(utils::formatBytes(1536) == "1.5 KiB");
    assert(utils::formatBytes(1024ULL * 1024) == "1.0 MiB");
    assert(utils::formatBytes(1024ULL * 1024 * 1024) == "1.0 GiB");
    std::cout << "  formatBytes OK\n";
}

static void testProtection() {
    assert(utils::isProtectedPath("/etc/passwd"));
    assert(utils::isProtectedPath("/usr/bin/ls"));
    assert(utils::isProtectedPath("/boot/vmlinuz"));
    assert(utils::isProtectedPath("/proc/cpuinfo"));
    assert(!utils::isProtectedPath("/home/user/.cache"));
    assert(!utils::isProtectedPath("/home/user/Downloads"));

    // Prefix trick must not match (/etcetera is not /etc)
    assert(!utils::isProtectedPath("/etcetera"));
    std::cout << "  isProtectedPath OK\n";
}

static void testFsTypes() {
    assert(utils::isPseudoFilesystem("proc"));
    assert(utils::isPseudoFilesystem("sysfs"));
    assert(utils::isPseudoFilesystem("TMPFS")); // case-insensitive
    assert(!utils::isPseudoFilesystem("ext4"));
    assert(!utils::isPseudoFilesystem("btrfs"));

    assert(utils::isNetworkFilesystem("nfs"));
    assert(utils::isNetworkFilesystem("cifs"));
    assert(!utils::isNetworkFilesystem("ext4"));
    std::cout << "  fs type helpers OK\n";
}

static void testEnums() {
    assert(utils::riskLevelToString(RiskLevel::Safe) == "Safe");
    assert(utils::riskLevelToString(RiskLevel::SystemProtected) == "System Protected");
    assert(utils::confidenceLevelToString(ConfidenceLevel::Medium) == "Medium");
    assert(!utils::skipReasonToString(SkipReason::PermissionDenied).empty());
    std::cout << "  enum helpers OK\n";
}

static void testIsPathUnder() {
    assert(utils::isPathUnder("/a/b/c", "/a/b"));
    assert(utils::isPathUnder("/a/b", "/a/b"));       // equal counts as under
    assert(utils::isPathUnder("/a/b", "/"));          // root contains everything
    assert(!utils::isPathUnder("/a/bc", "/a/b"));     // component boundary
    assert(!utils::isPathUnder("/etcetera", "/etc")); // prefix trick
    assert(!utils::isPathUnder("/a", "/a/b"));        // parent is not under child
    std::cout << "  isPathUnder OK\n";
}

static void testCanonicalizeNoFollow() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
                          ("driverse_canon_" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base / "real");

    // A symlink whose target is a directory: canonicalize must NOT follow it.
    const fs::path link = base / "link";
    std::error_code ec;
    fs::create_directory_symlink(base / "real", link, ec);
    if (!ec) {
        const fs::path canon = utils::canonicalizeNoFollow(link);
        assert(canon.filename() == "link"); // still the link itself
        // ...but the ancestor component is resolved (temp dir may itself be a link).
        assert(canon.parent_path() == fs::weakly_canonical(base));
    }

    // A nonexistent leaf under an existing parent still resolves the parent.
    const fs::path missing = base / "real" / "does-not-exist";
    const fs::path cm = utils::canonicalizeNoFollow(missing);
    assert(cm.filename() == "does-not-exist");
    assert(cm.parent_path() == fs::weakly_canonical(base / "real"));

    fs::remove_all(base);
    std::cout << "  canonicalizeNoFollow OK\n";
}

static void testErrnoAndAbbreviate() {
    assert(!utils::describeErrno(ENOENT).empty());
    assert(utils::describeErrno(ENOENT) ==
           std::error_code(ENOENT, std::generic_category()).message());

    // Short paths pass through unchanged.
    assert(utils::abbreviatePath("/tmp/x", 40) == "/tmp/x");

    // Long paths are elided at the middle, keeping root and filename.
    const std::filesystem::path longPath =
        "/home/alice/this/is/a/very/long/path/to/some/file.txt";
    const std::string abbr = utils::abbreviatePath(longPath, 20);
    assert(abbr == "/home/.../file.txt");
    assert(abbr.size() <= 20);

    // maxLen == 0 disables abbreviation.
    assert(utils::abbreviatePath(longPath, 0) == longPath.string());
    std::cout << "  describeErrno/abbreviatePath OK\n";
}

int main() {
    std::cout << "Running utils tests...\n";
    testFormatBytes();
    testProtection();
    testFsTypes();
    testEnums();
    testIsPathUnder();
    testCanonicalizeNoFollow();
    testErrnoAndAbbreviate();
    std::cout << "All utils tests passed!\n";
    return 0;
}
