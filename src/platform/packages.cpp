#include "packages.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace driverse::platform {

namespace {

namespace fs = std::filesystem;

bool isDigits(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::uint64_t toUint64(const std::string& s) {
    if (!isDigits(s)) return 0;
    std::uint64_t v = 0;
    for (const char c : s) {
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return v;
}

// Split off the first whitespace-delimited token(s) of a line.
std::vector<std::string> splitWhitespace(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

// Split a line on tabs; empty fields are preserved.
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// `snap list` hangs (no timeout, no error) when snapd is not running, so the
// backend is only offered while the snapd socket exists. The per-backend
// timeout stays as a safety net for a wedged daemon.
bool snapBackendUsable(const PackageInventory::Options& opts) {
    return !findExecutable("snap").empty() && fs::exists(opts.snapSocket);
}

} // namespace

std::optional<std::string> runReadOnlyQuery(const std::string& command,
                                            std::chrono::milliseconds timeout,
                                            std::size_t maxOutput) {
    int fds[2];
    if (::pipe(fds) != 0) {
        return std::nullopt;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return std::nullopt;
    }
    if (pid == 0) {
        // Child: send stdout/stderr into the pipe and run the query. All of
        // the backends are read-only commands built by us, never user input.
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);
        ::execl("/bin/sh", "sh", "-c", command.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(fds[1]);
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);

    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool timedOut = false;
    bool tooLarge = false;
    bool eof = false;

    while (!eof && !tooLarge) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            timedOut = true;
            break;
        }
        struct pollfd pfd {};
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        const int waitMs =
            static_cast<int>(std::min<std::int64_t>(remaining.count(), 100));
        const int pr = ::poll(&pfd, 1, waitMs);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue; // tick: re-check the deadline

        // Readable or hung up — drain whatever is available.
        for (;;) {
            char buf[8192];
            const ssize_t n = ::read(fds[0], buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                if (out.size() > maxOutput) {
                    tooLarge = true;
                    break;
                }
                continue;
            }
            if (n == 0) {
                eof = true;
                break;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break; // read error
        }
    }

    ::close(fds[0]);

    if (timedOut || tooLarge) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        return std::nullopt;
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        return std::nullopt;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::nullopt; // backend failure (often "not installed")
    }
    return out;
}

std::string findExecutable(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (path == nullptr || name.empty()) return {};
    std::size_t start = 0;
    const std::string p(path);
    while (start <= p.size()) {
        const std::size_t colon = p.find(':', start);
        const std::string dir = p.substr(start, colon == std::string::npos
                                                    ? std::string::npos
                                                    : colon - start);
        if (!dir.empty()) {
            const fs::path candidate = fs::path(dir) / name;
            std::error_code ec;
            if (::access(candidate.c_str(), X_OK) == 0 &&
                !fs::is_directory(candidate, ec)) {
                return candidate.string();
            }
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return {};
}

std::vector<PackageInfo> PackageInventory::parseDpkg(const std::string& output) {
    // dpkg-query -W -f='${Package}\t${Version}\t${Installed-Size}\n'
    // Installed-Size is in KiB.
    std::vector<PackageInfo> out;
    for (const auto& line : splitLines(output)) {
        const auto fields = splitTabs(line);
        if (fields.size() < 3 || fields[0].empty()) continue;
        PackageInfo p;
        p.name = fields[0];
        p.version = fields[1];
        p.sizeBytes = toUint64(fields[2]) * 1024u;
        p.source = "dpkg";
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PackageInfo> PackageInventory::parseRpm(const std::string& output) {
    // rpm -qa --qf '%{NAME}\t%{VERSION}-%{RELEASE}\t%{SIZE}\n' (SIZE in bytes)
    std::vector<PackageInfo> out;
    for (const auto& line : splitLines(output)) {
        const auto fields = splitTabs(line);
        if (fields.size() < 3 || fields[0].empty()) continue;
        PackageInfo p;
        p.name = fields[0];
        p.version = fields[1];
        p.sizeBytes = toUint64(fields[2]);
        p.source = "rpm";
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PackageInfo> PackageInventory::parseFlatpak(const std::string& output) {
    // flatpak list --columns=application,version,installed-size
    // (installed-size is reported in KiB; the size column may be missing.)
    std::vector<PackageInfo> out;
    for (const auto& line : splitLines(output)) {
        const auto fields = splitTabs(line);
        if (fields.empty() || fields[0].empty()) continue;
        PackageInfo p;
        p.name = fields[0];
        if (fields.size() > 1) p.version = fields[1];
        if (fields.size() > 2) p.sizeBytes = toUint64(fields[2]) * 1024u;
        p.source = "flatpak";
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PackageInfo> PackageInventory::parseSnapList(const std::string& output) {
    // "Name  Version  Rev  Tracking  Publisher  Notes" table; sizes come from
    // the snapshot files on disk, not from this table.
    std::vector<PackageInfo> out;
    bool headerSkipped = false;
    for (const auto& line : splitLines(output)) {
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        if (!headerSkipped) {
            headerSkipped = true;
            if (!line.empty() && line.rfind("Name", 0) == 0) continue; // table header
            if (line.rfind("no snaps installed", 0) == 0) continue;
        }
        const auto fields = splitWhitespace(line);
        if (fields.size() < 2) continue;
        PackageInfo p;
        p.name = fields[0];
        p.version = fields[1];
        p.source = "snap";
        out.push_back(std::move(p));
    }
    return out;
}

std::optional<PackageInfo> PackageInventory::parsePacmanDesc(const std::string& content) {
    // One /var/lib/pacman/local/<pkg>/desc: "%FIELD%\nvalue\n" blocks.
    PackageInfo p;
    std::string field;
    for (const auto& line : splitLines(content)) {
        if (line.size() > 2 && line.front() == '%' && line.back() == '%') {
            field = line.substr(1, line.size() - 2);
            continue;
        }
        if (line.empty() || field.empty()) continue;
        if (field == "NAME") p.name = line;
        else if (field == "VERSION") p.version = line;
        else if (field == "SIZE") p.sizeBytes = toUint64(line);
    }
    if (p.name.empty()) return std::nullopt;
    p.source = "pacman";
    return p;
}

std::vector<PackageInfo> readPacmanLocalDb(const std::filesystem::path& dbDir) {
    std::vector<PackageInfo> out;
    std::error_code ec;
    fs::directory_iterator it(dbDir, ec);
    if (ec) return out;
    for (const auto& entry : it) {
        if (!entry.is_directory(ec) || ec) continue;
        std::ifstream in(entry.path() / "desc");
        if (!in) continue;
        std::ostringstream buf;
        buf << in.rdbuf();
        if (auto p = PackageInventory::parsePacmanDesc(buf.str())) {
            out.push_back(std::move(*p));
        }
    }
    return out;
}

std::uint64_t snapBytesOnDisk(const std::string& snapName,
                              const std::filesystem::path& snapDir) {
    if (snapName.empty()) return 0;
    std::uint64_t total = 0;
    std::error_code ec;
    fs::directory_iterator it(snapDir, ec);
    if (ec) return 0;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || ec) continue;
        const std::string filename = entry.path().filename().string();
        // <name>_<revision>.snap
        if (filename.rfind(snapName + "_", 0) != 0) continue;
        if (filename.size() <= snapName.size() + 5) continue;
        if (filename.compare(filename.size() - 5, 5, ".snap") != 0) continue;
        total += static_cast<std::uint64_t>(entry.file_size(ec));
        if (ec) break;
    }
    return total;
}

std::string removalHint(const PackageInfo& pkg) {
    if (pkg.name.empty()) return {};
    if (pkg.source == "dpkg") {
        return findExecutable("apt").empty() ? "sudo dpkg -P " + pkg.name
                                             : "sudo apt remove " + pkg.name;
    }
    if (pkg.source == "pacman") return "sudo pacman -Rns " + pkg.name;
    if (pkg.source == "rpm") {
        return findExecutable("dnf").empty() ? "sudo rpm -e " + pkg.name
                                             : "sudo dnf remove " + pkg.name;
    }
    if (pkg.source == "flatpak") return "flatpak uninstall " + pkg.name;
    if (pkg.source == "snap") return "sudo snap remove " + pkg.name;
    return {};
}

std::vector<std::string> PackageInventory::availableBackends() {
    return availableBackends(Options{});
}

std::vector<std::string> PackageInventory::availableBackends(const Options& opts) {
    std::vector<std::string> out;
    if (!findExecutable("dpkg-query").empty()) out.emplace_back("dpkg");
    if (!findExecutable("rpm").empty()) out.emplace_back("rpm");
    if (!findExecutable("flatpak").empty()) out.emplace_back("flatpak");
    if (snapBackendUsable(opts)) out.emplace_back("snap");
    std::error_code ec;
    if (fs::is_directory(opts.pacmanDb, ec)) out.emplace_back("pacman");
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<PackageInfo> PackageInventory::inventory() const {
    return inventory(Options{});
}

std::vector<PackageInfo> PackageInventory::inventory(const Options& opts) const {
    // Backends are independent and read-only, so query them in parallel: the
    // wall-clock wait is bounded by the slowest backend (its timeout), not the
    // sum of all of them.
    std::vector<std::function<std::vector<PackageInfo>()>> tasks;

    if (!findExecutable("dpkg-query").empty()) {
        tasks.emplace_back([&opts]() {
            if (auto s = runReadOnlyQuery(
                    "dpkg-query -W -f='${Package}\\t${Version}\\t${Installed-Size}\\n'",
                    opts.timeout)) {
                return PackageInventory::parseDpkg(*s);
            }
            return std::vector<PackageInfo>{};
        });
    }
    if (fs::is_directory(opts.pacmanDb)) {
        // Direct metadata read: no subprocess, so no timeout needed.
        tasks.emplace_back([&opts]() { return readPacmanLocalDb(opts.pacmanDb); });
    }
    if (!findExecutable("rpm").empty()) {
        tasks.emplace_back([&opts]() {
            if (auto s = runReadOnlyQuery(
                    "rpm -qa --qf '%{NAME}\\t%{VERSION}-%{RELEASE}\\t%{SIZE}\\n'",
                    opts.timeout)) {
                return PackageInventory::parseRpm(*s);
            }
            return std::vector<PackageInfo>{};
        });
    }
    if (!findExecutable("flatpak").empty()) {
        tasks.emplace_back([&opts]() {
            if (auto s = runReadOnlyQuery(
                    "flatpak list --columns=application,version,installed-size",
                    opts.timeout)) {
                return PackageInventory::parseFlatpak(*s);
            }
            return std::vector<PackageInfo>{};
        });
    }
    if (snapBackendUsable(opts)) {
        tasks.emplace_back([&opts]() {
            if (auto s = runReadOnlyQuery("snap list", opts.timeout)) {
                auto snaps = PackageInventory::parseSnapList(*s);
                for (auto& p : snaps) {
                    p.sizeBytes = snapBytesOnDisk(p.name, opts.snapDir);
                }
                return snaps;
            }
            return std::vector<PackageInfo>{};
        });
    }

    std::vector<std::vector<PackageInfo>> results(tasks.size());
    std::vector<std::thread> threads;
    threads.reserve(tasks.size());
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        threads.emplace_back([i, &tasks, &results]() { results[i] = tasks[i](); });
    }
    for (auto& t : threads) t.join();

    std::vector<PackageInfo> out;
    for (auto& r : results) {
        out.insert(out.end(), std::make_move_iterator(r.begin()),
                   std::make_move_iterator(r.end()));
    }

    // Biggest first; name as the tie-breaker for a stable, useful default.
    std::sort(out.begin(), out.end(),
              [](const PackageInfo& a, const PackageInfo& b) {
                  if (a.sizeBytes != b.sizeBytes) return a.sizeBytes > b.sizeBytes;
                  if (a.source != b.source) return a.source < b.source;
                  return a.name < b.name;
              });
    return out;
}

} // namespace driverse::platform
