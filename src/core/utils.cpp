#include "utils.h"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace driverse::core::utils {

std::string formatBytes(std::uintmax_t bytes) {
    if (bytes == 0) return "0 B";
    
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    
    while (size >= 1024.0 && unit < 5) {
        size /= 1024.0;
        ++unit;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << size << " " << units[unit];
    return oss.str();
}

std::string formatDuration(std::chrono::milliseconds ms) {
    auto totalMs = ms.count();
    if (totalMs < 1000) {
        return std::to_string(totalMs) + " ms";
    }
    auto seconds = totalMs / 1000;
    auto remMs = totalMs % 1000;
    if (seconds < 60) {
        std::ostringstream oss;
        oss << seconds << "." << std::setfill('0') << std::setw(3) << remMs << " s";
        return oss.str();
    }
    auto minutes = seconds / 60;
    auto remSec = seconds % 60;
    return std::to_string(minutes) + "m " + std::to_string(remSec) + "s";
}

std::vector<std::filesystem::path> getDefaultProtectedPaths() {
    return {
        "/boot",
        "/efi",
        "/usr",
        "/etc",
        "/var/lib",
        "/proc",
        "/sys",
        "/dev",
        "/run",
        "/snap",
        "/var/spool/cron",
        "/var/log",
        "/boot/efi"
    };
}

std::vector<std::filesystem::path> getDefaultExcludedPaths() {
    return {
        "/proc",
        "/sys",
        "/dev",
        "/run",
        "/sys/kernel/debug"
    };
}

std::vector<std::string> getDefaultExcludedFsTypes() {
    return {
        "proc",
        "sysfs",
        "devtmpfs",
        "devpts",
        "tmpfs",
        "cgroup",
        "cgroup2",
        "fuse",
        "fusectl",
        "overlay",
        "squashfs",
        "sysfs",
        "debugfs",
        "tracefs",
        "securityfs",
        "pstore",
        "efivarfs",
        "bpf"
    };
}

// Returns true if `path` equals or is nested under `root` (component-wise, no symlink resolution).
static bool isUnder(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto p = path.lexically_normal();
    auto r = root.lexically_normal();
    if (p == r) {
        return true;
    }

    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++pit) {
        if (pit == p.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

bool isProtectedPath(const std::filesystem::path& path) {
    auto absPath = std::filesystem::absolute(path);
    for (const auto& prot : getDefaultProtectedPaths()) {
        if (isUnder(absPath, std::filesystem::absolute(prot))) {
            return true;
        }
    }
    return false;
}

std::filesystem::path normalizePath(const std::filesystem::path& path) {
    try {
        // Don't follow symlinks, just normalize lexical components
        return std::filesystem::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

std::string skipReasonToString(SkipReason reason) {
    switch (reason) {
        case SkipReason::PermissionDenied: return "Permission denied";
        case SkipReason::NotAccessible: return "Not accessible";
        case SkipReason::LoopDetected: return "Filesystem cycle detected (bind mount)";
        case SkipReason::CrossFilesystem: return "Cross-filesystem boundary";
        case SkipReason::ProtectedPath: return "Protected system path";
        case SkipReason::IsSymlink: return "Symbolic link (not followed)";
        case SkipReason::PseudoFilesystem: return "Pseudo-filesystem";
        case SkipReason::Other: return "Other reason";
        default: return "Unknown";
    }
}

std::string riskLevelToString(RiskLevel risk) {
    switch (risk) {
        case RiskLevel::Safe: return "Safe";
        case RiskLevel::Low: return "Low";
        case RiskLevel::Medium: return "Medium";
        case RiskLevel::High: return "High";
        case RiskLevel::SystemProtected: return "System Protected";
        default: return "Unknown";
    }
}

std::string confidenceLevelToString(ConfidenceLevel confidence) {
    switch (confidence) {
        case ConfidenceLevel::High: return "High";
        case ConfidenceLevel::Medium: return "Medium";
        case ConfidenceLevel::Low: return "Low";
        default: return "Unknown";
    }
}

bool isPseudoFilesystem(const std::string& fsType) {
    std::string type = fsType;
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    auto excluded = getDefaultExcludedFsTypes();
    for (const auto& ex : excluded) {
        if (type == ex) return true;
    }
    return false;
}

bool isNetworkFilesystem(const std::string& fsType) {
    std::string type = fsType;
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    if (type == "nfs" || type == "nfs4" || type == "smb" || type == "cifs" ||
        type == "afp" || type == "nfsd" || type == "sshfs" || type == "fuse.sshfs") {
        return true;
    }
    return false;
}

} // namespace driverse::core::utils
