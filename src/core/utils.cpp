#include "utils.h"
#include <cerrno>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
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

bool isPathUnderNormalized(const std::string& child, const std::string& parent) {
    if (parent.empty()) {
        return false;
    }
    if (parent == "/") {
        return child.size() >= 1 && child[0] == '/';
    }
    if (child.size() < parent.size()) {
        return false;
    }
    if (child.compare(0, parent.size(), parent) != 0) {
        return false;
    }
    return child.size() == parent.size() || child[parent.size()] == '/';
}

bool isProtectedPath(const std::filesystem::path& path) {
    // The default list is a compile-time constant: normalize it once instead
    // of per call. This runs for every scanned entry, so paying
    // absolute()+lexically_normal() for ~20 roots each time adds up on large
    // scans.
    static const std::vector<std::string> kDefaults = [] {
        std::vector<std::string> out;
        out.reserve(32);
        for (const auto& prot : getDefaultProtectedPaths()) {
            // normalizedAbsoluteString() also strips trailing slashes, so the
            // component-boundary prefix test below stays exact.
            out.push_back(normalizedAbsoluteString(prot));
        }
        return out;
    }();
    const std::string abs = normalizedAbsoluteString(path);
    for (const auto& prot : kDefaults) {
        if (isPathUnderNormalized(abs, prot)) {
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

namespace {

// True when `s` is an absolute path that lexically_normal() would return
// unchanged: no empty ("//"), "." or ".." components and no trailing slash.
bool isLexicallyNormalAbsolute(std::string_view s) {
    if (s.empty() || s.front() != '/') {
        return false;
    }
    if (s.size() == 1) {
        return true; // root
    }
    if (s.back() == '/') {
        return false;
    }
    if (s.find("//") != std::string_view::npos ||
        s.find("/./") != std::string_view::npos ||
        s.find("/../") != std::string_view::npos) {
        return false;
    }
    return !(s.ends_with("/.") || s.ends_with("/.."));
}

} // namespace

std::string normalizedAbsoluteString(const std::filesystem::path& path) {
    // Fast path: every path produced by the scanner is already absolute and
    // lexically normal. absolute()/lexically_normal() then amount to an
    // expensive re-parse/rebuild, which adds up when classifying millions of
    // entries — so validate with a string scan and reuse the string as-is.
    const std::string s = path.string();
    if (!s.empty() && s.front() == '/' && isLexicallyNormalAbsolute(s)) {
        return s;
    }
    std::string out = std::filesystem::absolute(path).lexically_normal().string();
    // lexically_normal() keeps a trailing separator ("/a/b/" -> "/a/b/") and
    // config-supplied paths often carry one. Strip it (root excepted): every
    // caller compares these strings at component boundaries, where a trailing
    // "/" would make "/boot/" fail to match "/boot/efi".
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
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
