#include "blockdev.h"

#include <cctype>
#include <fstream>
#include <system_error>

namespace driverse::platform {

namespace {

namespace fs = std::filesystem;

bool fsExists(const fs::path& p) {
    std::error_code ec;
    const bool r = fs::exists(p, ec);
    return !ec && r;
}

// "sda1" -> "sda", "nvme0n1p3" -> "nvme0n1", "mmcblk0p1" -> "mmcblk0".
// Only answers when the candidate really exists under <sysfs>/block, so a
// guess can never invent a disk.
std::string diskNameFromPartition(const std::string& part,
                                  const fs::path& sysfsRoot) {
    std::size_t end = part.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(part[end - 1]))) {
        --end;
    }
    if (end == 0 || end == part.size()) {
        return {};
    }
    // Digits stripped: "sda1" -> "sda".
    const std::string plain = part.substr(0, end);
    if (fsExists(sysfsRoot / "block" / plain)) {
        return plain;
    }
    // Trailing 'p' separator: "nvme0n1p3" -> "nvme0n1".
    if (plain.size() > 1 && plain.back() == 'p') {
        const std::string parent = plain.substr(0, plain.size() - 1);
        if (fsExists(sysfsRoot / "block" / parent)) {
            return parent;
        }
    }
    return {};
}

} // namespace

std::string kernelBlockName(const std::string& device) {
    if (device.rfind("/dev/", 0) != 0) {
        return {};
    }
    const std::string rest = device.substr(5);
    if (rest.empty() || rest[0] == '/') {
        return {};
    }
    // Plain block nodes only; /dev/mapper/*, /dev/disk/by-* and friends are
    // links/aliases whose kernel name we cannot derive here.
    if (rest.find('/') != std::string::npos) {
        return {};
    }
    return rest;
}

BlockDevice queryBlockDevice(const std::string& device,
                             const fs::path& sysfsRoot) {
    BlockDevice out;
    const std::string name = kernelBlockName(device);
    if (name.empty()) {
        return out;
    }

    const fs::path classDir = sysfsRoot / "class" / "block" / name;
    if (!fsExists(classDir)) {
        return out; // not a block device this kernel knows about
    }

    out.isPartition = fsExists(classDir / "partition");
    if (out.isPartition) {
        // Real sysfs: class/block/<part> is a symlink into the device tree,
        // and the parent directory of ".../block/<disk>/<part>" is the disk.
        std::error_code ec;
        const fs::path resolved = fs::weakly_canonical(classDir, ec);
        if (!ec) {
            for (fs::path dir = resolved.parent_path();
                 !dir.empty() && dir != dir.root_path(); dir = dir.parent_path()) {
                if (fsExists(sysfsRoot / "block" / dir.filename())) {
                    out.disk = dir.filename().string();
                    break;
                }
            }
        }
        // Fallback (and for fixtures): derive the disk from the name.
        if (out.disk.empty()) {
            out.disk = diskNameFromPartition(name, sysfsRoot);
        }
    } else {
        out.disk = name; // top-level disk
    }

    if (!out.disk.empty()) {
        std::ifstream in(sysfsRoot / "block" / out.disk / "removable");
        std::string value;
        if (in >> value) {
            out.isRemovable = (value == "1");
        }
    }
    return out;
}

} // namespace driverse::platform
