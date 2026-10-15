#include "core/types.h"
#include <iostream>
#include <cassert>

using namespace driverse::core;

int main() {
    FileItem item;
    item.path = "/tmp/test.txt";
    item.name = "test.txt";
    item.logicalSize = 1024;
    item.allocatedSize = 4096;
    item.inode = 12345;
    item.deviceId = 65024;
    item.type = FileType::RegularFile;
    item.isSymlink = false;
    item.isDirectory = false;
    item.isHardLinked = false;
    item.isProtected = false;
    item.isSparse = false;
    item.hardLinkCount = 1;
    item.sizeConfidence = ConfidenceLevel::Medium;
    
    assert(item.logicalSize == 1024);
    assert(item.allocatedSize == 4096);
    
    DeviceInfo dev;
    dev.mountPoint = "/";
    dev.fsType = "ext4";
    dev.totalBytes = 1024ULL * 1024ULL * 1024ULL;
    assert(dev.mountPoint == "/");
    
    CleanupCandidate cand;
    cand.path = "/home/user/.cache/test";
    cand.risk = RiskLevel::Low;
    cand.confidence = ConfidenceLevel::High;
    cand.justification = "Test cache";
    assert(cand.risk == RiskLevel::Low);
    
    std::cout << "All type tests passed!\n";
    return 0;
}
