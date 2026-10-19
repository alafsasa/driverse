#pragma once

#include <filesystem>
#include <string>

namespace driverse::platform {

// Where a mount's backing block device sits in the device hierarchy (G2),
// resolved through sysfs. Unknown for non-block sources (tmpfs, overlay,
// network shares, /dev/mapper nodes).
struct BlockDevice {
    std::string disk;         // top-level disk ("sda", "nvme0n1"); "" if unknown
    bool isPartition = false; // partition of a larger disk
    bool isRemovable = false; // top-level disk reports removable media (USB ...)

    bool known() const { return !disk.empty(); }
};

// Classify the mount source "/dev/sda1" (or a bare "sda1"). `sysfsRoot` is
// injectable so the classifier can be exercised against a fixture tree.
BlockDevice queryBlockDevice(const std::string& device,
                             const std::filesystem::path& sysfsRoot = "/sys");

// Kernel block-device name for a mount source ("/dev/sda1" -> "sda1").
// Empty when the source is not a plain block node ("/dev/mapper/x",
// "tmpfs", "server:/share", ...).
std::string kernelBlockName(const std::string& device);

} // namespace driverse::platform
