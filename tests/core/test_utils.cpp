#include "core/utils.h"

#include <iostream>
#include <cassert>
#include <cmath>

using namespace driverse::core;

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

int main() {
    std::cout << "Running utils tests...\n";
    testFormatBytes();
    testProtection();
    testFsTypes();
    testEnums();
    std::cout << "All utils tests passed!\n";
    return 0;
}
