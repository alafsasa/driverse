#pragma once

#include "core/types.h"
#include <vector>
#include <string>
#include <filesystem>

namespace driverse::platform {

class MountDetector {
public:
    // Discover all mounts from /proc/self/mountinfo or /proc/mounts
    static std::vector<core::DeviceInfo> discoverMounts();
    
    // Get mounts filtered to user-relevant local filesystems
    static std::vector<core::DeviceInfo> getRelevantMounts();
    
    // Get mount for a specific path (the filesystem the path resides on)
    static std::optional<core::DeviceInfo> getMountForPath(const std::filesystem::path& path);
    
    // Get capacity info via statvfs
    static bool getCapacity(const std::filesystem::path& mountPoint, core::DeviceInfo& info);
    
private:
    static core::DeviceInfo parseMountInfoLine(const std::string& line);
    static std::string unescapeMountField(const std::string& field);
};

} // namespace driverse::platform
