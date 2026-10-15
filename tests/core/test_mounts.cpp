#include "platform/mounts.h"
#include "core/utils.h"

#include <iostream>
#include <cassert>
#include <algorithm>

using namespace driverse::platform;

int main() {
    std::cout << "Running mounts tests...\n";

    auto mounts = MountDetector::discoverMounts();
    assert(!mounts.empty() && "Should discover at least the root filesystem");

    // Root must always exist and be a usable local fs.
    bool foundRoot = false;
    for (const auto& m : mounts) {
        if (m.mountPoint == "/") {
            foundRoot = true;
            std::cout << "  root: fs=" << m.fsType
                      << " total=" << driverse::core::utils::formatBytes(m.totalBytes)
                      << " used=" << driverse::core::utils::formatBytes(m.usedBytes)
                      << " free=" << driverse::core::utils::formatBytes(m.freeBytes)
                      << " ro=" << m.isReadOnly << "\n";
            assert(m.totalBytes > 0);
            assert(m.usedBytes <= m.totalBytes);
        }
        assert(!m.mountPoint.empty());
    }
    assert(foundRoot);

    auto relevant = MountDetector::getRelevantMounts();
    for (const auto& m : relevant) {
        assert(!m.isPseudoFs && "Relevant mounts must not include pseudo filesystems");
        assert(!m.isNetworkFs);
    }
    std::cout << "  relevant mounts: " << relevant.size()
              << " / total: " << mounts.size() << "\n";

    auto rootMount = MountDetector::getMountForPath("/etc/hostname");
    assert(rootMount.has_value());
    assert(rootMount->mountPoint == "/");
    std::cout << "  getMountForPath(/etc/hostname) -> " << rootMount->mountPoint << "\n";

    std::cout << "All mounts tests passed!\n";
    return 0;
}
