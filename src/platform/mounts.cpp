#include "mounts.h"
#include "blockdev.h"
#include "core/utils.h"

#include <fstream>
#include <sstream>
#include <cstring>

#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

namespace driverse::platform {

using namespace driverse::core;

std::string MountDetector::unescapeMountField(const std::string& field) {
    // /proc mount fields escape space as \040, tab as \011, newline as \012, backslash as \134
    std::string result;
    result.reserve(field.size());
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            std::string oct = field.substr(i + 1, 3);
            bool isOctal = oct.size() == 3 &&
                oct[0] >= '0' && oct[0] <= '7' &&
                oct[1] >= '0' && oct[1] <= '7' &&
                oct[2] >= '0' && oct[2] <= '7';
            if (isOctal) {
                char c = static_cast<char>(std::stoi(oct, nullptr, 8));
                result.push_back(c);
                i += 3;
                continue;
            }
        }
        result.push_back(field[i]);
    }
    return result;
}

bool MountDetector::getCapacity(const std::filesystem::path& mountPoint, DeviceInfo& info) {
    struct statvfs vfs;
    if (::statvfs(mountPoint.c_str(), &vfs) != 0) {
        return false;
    }

    const std::uintmax_t f_frsize = vfs.f_frsize != 0 ? vfs.f_frsize : vfs.f_bsize;
    info.totalBytes     = static_cast<std::uintmax_t>(vfs.f_blocks) * f_frsize;
    info.freeBytes      = static_cast<std::uintmax_t>(vfs.f_bfree)  * f_frsize;
    info.availableBytes = static_cast<std::uintmax_t>(vfs.f_bavail) * f_frsize;
    info.usedBytes      = info.totalBytes - info.freeBytes;
    info.isReadOnly     = (vfs.f_flag & ST_RDONLY) != 0;

    struct stat st;
    if (::stat(mountPoint.c_str(), &st) == 0) {
        info.deviceId = static_cast<std::uintmax_t>(st.st_dev);
    }
    return true;
}

DeviceInfo MountDetector::parseMountInfoLine(const std::string& line) {
    DeviceInfo info;
    std::istringstream iss(line);
    std::string token;
    std::vector<std::string> preSeparator;
    std::vector<std::string> postSeparator;
    bool afterSep = false;

    while (iss >> token) {
        if (!afterSep && token == "-") {
            afterSep = true;
            continue;
        }
        if (afterSep) {
            postSeparator.push_back(token);
        } else {
            preSeparator.push_back(token);
        }
    }

    // mountinfo pre-separator: id parentid major:minor root mountpoint options [optional...]
    if (preSeparator.size() >= 5) {
        info.mountPoint = unescapeMountField(preSeparator[4]);
        info.options = preSeparator[5 < preSeparator.size() ? 5 : preSeparator.size() - 1];
    }

    if (!postSeparator.empty()) {
        info.fsType = postSeparator[0];
        if (postSeparator.size() >= 2) {
            info.device = unescapeMountField(postSeparator[1]);
        }
    }

    info.isPseudoFs = utils::isPseudoFilesystem(info.fsType);
    info.isNetworkFs = utils::isNetworkFilesystem(info.fsType);

    // G2: classify the backing block device (partition vs top-level disk,
    // removable media). Unknown for non-block sources.
    const BlockDevice bd = queryBlockDevice(info.device);
    info.parentDisk = bd.disk;
    info.isPartition = bd.isPartition;
    info.isRemovable = bd.isRemovable;

    if (!info.mountPoint.empty()) {
        getCapacity(info.mountPoint, info);
    }
    return info;
}

std::vector<DeviceInfo> MountDetector::discoverMounts() {
    std::vector<DeviceInfo> mounts;
    std::ifstream file("/proc/self/mountinfo");
    if (!file.is_open()) {
        return mounts;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto info = parseMountInfoLine(line);
        if (!info.mountPoint.empty()) {
            mounts.push_back(std::move(info));
        }
    }
    return mounts;
}

std::vector<DeviceInfo> MountDetector::getRelevantMounts() {
    std::vector<DeviceInfo> result;
    for (auto& m : discoverMounts()) {
        if (m.isPseudoFs) continue;
        if (m.isNetworkFs) continue;
        if (m.mountPoint.empty()) continue;
        result.push_back(std::move(m));
    }
    return result;
}

std::optional<DeviceInfo> MountDetector::getMountForPath(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) canonical = std::filesystem::absolute(path, ec);

    // Find the longest mount point that is a prefix of the path
    std::optional<DeviceInfo> best;
    std::size_t bestLen = 0;
    const std::string p = canonical.string();
    for (auto& m : discoverMounts()) {
        const std::string& mp = m.mountPoint;
        if (mp.empty()) continue;

        bool matches = false;
        if (p == mp) {
            matches = true;
        } else if (mp == "/") {
            matches = !p.empty() && p[0] == '/';
        } else if (p.size() > mp.size() &&
                   p.compare(0, mp.size(), mp) == 0 &&
                   p[mp.size()] == '/') {
            matches = true;
        }

        if (matches && mp.size() > bestLen) {
            best = m;
            bestLen = mp.size();
        }
    }
    return best;
}

} // namespace driverse::platform
