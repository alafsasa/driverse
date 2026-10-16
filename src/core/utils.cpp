#include "utils.h"
#include <cerrno>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

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

std::string describeErrno(int err) {
    return std::error_code(err, std::generic_category()).message();
}

std::string fsErrorHint(const std::error_code& ec) {
    if (!ec) {
        return {};
    }
    switch (ec.value()) {
        case ENOSPC:
            return "the device is full — free some space and retry";
        case EBUSY:
            return "in use by another process (close it and retry)";
        case EROFS:
            return "the filesystem is mounted read-only";
        case EACCES:
        case EPERM:
            return "permission denied (file owned by another user or parent not writable)";
        case EXDEV:
            return "source and destination are on different filesystems";
        case ENOTEMPTY:
            return "the directory is not empty";
        case ENOENT:
            return "it no longer exists (moved, deleted, or unmounted?)";
        case EIO:
            return "I/O error (media problem or unreachable remote filesystem?)";
        default:
            return {};
    }
}

std::string abbreviatePath(const std::filesystem::path& path, std::size_t maxLen) {
    const std::string s = path.string();
    if (maxLen == 0 || s.size() <= maxLen) {
        return s;
    }
    if (maxLen <= 3) {
        return "...";
    }

    // Keep the head (leading components) and the tail (filename / last dirs).
    const std::size_t keep = maxLen - 3; // room for "..."
    const std::size_t headLen = keep / 2;
    const std::size_t tailLen = keep - headLen;

    std::string head = s.substr(0, headLen);
    std::string tail = s.substr(s.size() - tailLen);

    // Trim to component boundaries so the result stays readable and does not
    // look like a partial filename.
    const auto slashHead = head.find_last_of('/');
    if (slashHead != std::string::npos && slashHead > 0) {
        head.resize(slashHead);
    }
    const auto slashTail = tail.find('/');
    if (slashTail != std::string::npos && slashTail + 1 < tail.size()) {
        tail.erase(0, slashTail + 1);
    }

    return head + "/..." + "/" + tail;
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

// Returns true if `child` equals or is nested under `parent`
// (component-wise, no symlink resolution).
bool isPathUnder(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto p = child.lexically_normal();
    auto r = parent.lexically_normal();
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
        if (isPathUnder(absPath, std::filesystem::absolute(prot))) {
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

std::filesystem::path canonicalizeNoFollow(const std::filesystem::path& path) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (parent.empty()) {
        // No ancestor to resolve; only lexical normalization is safe.
        return path.lexically_normal();
    }
    const auto canonParent = std::filesystem::weakly_canonical(parent, ec);
    if (ec) {
        return path.lexically_normal();
    }
    const auto name = path.filename();
    if (name.empty()) {
        return canonParent;
    }
    return (canonParent / name).lexically_normal();
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
