#include "platform/blockdev.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using driverse::platform::BlockDevice;
using driverse::platform::kernelBlockName;
using driverse::platform::queryBlockDevice;

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << content;
}

// A miniature sysfs: partitions under class/block (one as a symlink into the
// device tree, the way the kernel really lays it out), disks under block/.
fs::path makeFixtureSysfs(const fs::path& root) {
    fs::remove_all(root);
    fs::create_directories(root / "class" / "block");

    // Real sysfs: class/block/<part> is a symlink to .../block/<disk>/<part>,
    // and the partition marker lives in that device directory.
    fs::create_directories(root / "block" / "vda" / "vda1");
    writeFile(root / "block" / "vda" / "vda1" / "partition", "1");
    fs::create_directory_symlink(root / "block" / "vda" / "vda1",
                                 root / "class" / "block" / "vda1");

    // Plain-directory partitions (no symlink), resolved by the name fallback.
    writeFile(root / "class" / "block" / "sda1" / "partition", "1");
    writeFile(root / "class" / "block" / "nvme0n1p3" / "partition", "3");
    writeFile(root / "class" / "block" / "mmcblk0p1" / "partition", "1");

    // A top-level disk: no `partition` file.
    fs::create_directories(root / "class" / "block" / "loop0");

    // Disk entries with removable flags.
    writeFile(root / "block" / "sda" / "removable", "1\n");
    writeFile(root / "block" / "nvme0n1" / "removable", "0\n");
    writeFile(root / "block" / "mmcblk0" / "removable", "0\n");
    writeFile(root / "block" / "loop0" / "removable", "0\n");
    writeFile(root / "block" / "vda" / "removable", "0\n");
    return root;
}

void testKernelBlockName() {
    assert(kernelBlockName("/dev/sda1") == "sda1");
    assert(kernelBlockName("/dev/nvme0n1p3") == "nvme0n1p3");
    // Non-block sources: not /dev nodes, mapper paths, or link aliases.
    assert(kernelBlockName("tmpfs").empty());
    assert(kernelBlockName("server:/share").empty());
    assert(kernelBlockName("/dev/mapper/cryptroot").empty());
    assert(kernelBlockName("/dev/disk/by-uuid/1234-abcd").empty());
    assert(kernelBlockName("/dev/").empty());
    std::cout << "  kernelBlockName OK\n";
}

void testPartitionResolvesToDisk(const fs::path& sysfs) {
    const BlockDevice sda = queryBlockDevice("/dev/sda1", sysfs);
    assert(sda.known());
    assert(sda.disk == "sda");
    assert(sda.isPartition);
    assert(sda.isRemovable); // the fixture says so

    const BlockDevice nvme = queryBlockDevice("/dev/nvme0n1p3", sysfs);
    assert(nvme.known());
    assert(nvme.disk == "nvme0n1"); // "p" separator stripped, verified on disk
    assert(nvme.isPartition);
    assert(!nvme.isRemovable);

    const BlockDevice mmc = queryBlockDevice("/dev/mmcblk0p1", sysfs);
    assert(mmc.known());
    assert(mmc.disk == "mmcblk0");
    assert(mmc.isPartition);
    std::cout << "  partition -> disk (name fallback) OK\n";
}

void testSymlinkedClassEntry(const fs::path& sysfs) {
    // class/block/<part> as a symlink into .../block/<disk>/<part>.
    const BlockDevice vda = queryBlockDevice("/dev/vda1", sysfs);
    assert(vda.known());
    assert(vda.disk == "vda");
    assert(vda.isPartition);
    assert(!vda.isRemovable);
    std::cout << "  partition -> disk (device-tree walk) OK\n";
}

void testWholeDisk(const fs::path& sysfs) {
    const BlockDevice loop = queryBlockDevice("/dev/loop0", sysfs);
    assert(loop.known());
    assert(loop.disk == "loop0");
    assert(!loop.isPartition);
    assert(!loop.isRemovable);
    std::cout << "  whole disk OK\n";
}

void testUnknownSources(const fs::path& sysfs) {
    assert(!queryBlockDevice("tmpfs", sysfs).known());
    assert(!queryBlockDevice("/dev/mapper/cryptroot", sysfs).known());
    assert(!queryBlockDevice("/dev/disk/by-uuid/1234-abcd", sysfs).known());
    // A /dev node the kernel does not expose in sysfs stays unknown rather
    // than being guessed from its name.
    assert(!queryBlockDevice("/dev/zd0", sysfs).known());
    std::cout << "  unknown sources OK\n";
}

} // namespace

int main() {
    std::cout << "Running block device tests...\n";
    const fs::path root =
        fs::temp_directory_path() /
        ("driverse_blockdev_" + std::to_string(static_cast<long>(::getpid())));
    makeFixtureSysfs(root);

    testKernelBlockName();
    testPartitionResolvesToDisk(root);
    testSymlinkedClassEntry(root);
    testWholeDisk(root);
    testUnknownSources(root);

    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << "All block device tests passed!\n";
    return 0;
}
