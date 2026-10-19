#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace driverse::platform {

// One installed application as reported by a system package manager (Phase 6).
struct PackageInfo {
    std::string name;
    std::string version;
    std::uint64_t sizeBytes = 0; // 0 when the backend cannot report sizes
    std::string source;          // "dpkg" | "pacman" | "rpm" | "flatpak" | "snap"
};

// Query-only inventory of installed applications. Driverse never installs,
// removes or modifies anything: every backend is a read-only command (or a
// direct read of package metadata on disk). Slow or hung backends are killed
// at the timeout instead of being allowed to stall the caller.
//
// Sizes are the *installed footprint* reported by each backend; applications
// share dependencies, so removing one package frees less than its reported
// size and can break others. The UI is expected to say so.
class PackageInventory {
public:
    struct Options {
        std::chrono::milliseconds timeout{15000}; // per subprocess backend
        std::filesystem::path pacmanDb = "/var/lib/pacman/local";
        std::filesystem::path snapDir = "/var/lib/snapd/snaps";
        // Without the snapd socket the daemon is not running: `snap list`
        // would hang instead of failing, so the backend is skipped.
        std::filesystem::path snapSocket = "/run/snapd.socket";
    };

    // Query backends usable on this machine. Sorted by source name.
    static std::vector<std::string> availableBackends();
    static std::vector<std::string> availableBackends(const Options& opts);

    // Query every available backend; missing, failing or hung ones are
    // skipped. Results are sorted by size (largest first), then name.
    std::vector<PackageInfo> inventory() const;
    std::vector<PackageInfo> inventory(const Options& opts) const;

    // --- pure parsers, unit-tested against captured backend output ---
    static std::vector<PackageInfo> parseDpkg(const std::string& output);
    static std::vector<PackageInfo> parseRpm(const std::string& output);
    static std::vector<PackageInfo> parseFlatpak(const std::string& output);
    static std::vector<PackageInfo> parseSnapList(const std::string& output);
    static std::optional<PackageInfo> parsePacmanDesc(const std::string& content);
};

// Read every /var/lib/pacman/local/<pkg>/desc directly — far faster than
// `pacman -Qi` and needs no subprocess. Empty when the dir is unreadable.
std::vector<PackageInfo> readPacmanLocalDb(const std::filesystem::path& dbDir);

// Sum of <snapDir>/<name>_<rev>.snap file sizes (read-only); 0 when the snap
// has no snapshot file on disk.
std::uint64_t snapBytesOnDisk(const std::string& snapName,
                              const std::filesystem::path& snapDir);

// The command a user would run to remove a package — shown as *text* in the
// UI for copy/paste; Driverse never executes it. Empty when unknown.
std::string removalHint(const PackageInfo& pkg);

// Run a read-only query through /bin/sh. Empty when the command failed,
// exceeded the timeout, or produced more than `maxOutput` bytes. Exposed for
// tests; the UI only ever calls this with fixed, constant commands.
std::optional<std::string> runReadOnlyQuery(const std::string& command,
                                            std::chrono::milliseconds timeout,
                                            std::size_t maxOutput = 32u * 1024 * 1024);

// Locate an executable on PATH (empty when absent). Exposed for tests.
std::string findExecutable(const std::string& name);

} // namespace driverse::platform
