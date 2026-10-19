#include "platform/packages.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using driverse::platform::PackageInfo;
using driverse::platform::PackageInventory;
using driverse::platform::findExecutable;
using driverse::platform::readPacmanLocalDb;
using driverse::platform::removalHint;
using driverse::platform::runReadOnlyQuery;
using driverse::platform::snapBytesOnDisk;

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << content;
}

void testParseDpkg() {
    // dpkg-query -W -f='${Package}\t${Version}\t${Installed-Size}\n'
    const std::string out =
        "firefox\t128.0-1\t234567\n"
        "malformed line without tabs\n"
        "libx11-6\t1.8.7-1\t0\n"
        "\n";
    const auto pkgs = PackageInventory::parseDpkg(out);
    assert(pkgs.size() == 2);
    assert(pkgs[0].name == "firefox");
    assert(pkgs[0].version == "128.0-1");
    assert(pkgs[0].sizeBytes == 234567ull * 1024ull); // KiB -> bytes
    assert(pkgs[0].source == "dpkg");
    assert(pkgs[1].name == "libx11-6");
    assert(pkgs[1].sizeBytes == 0);
    std::cout << "  parseDpkg OK\n";
}

void testParseRpm() {
    const std::string out =
        "bash\t5.2.21-3\t1234567\n"
        "gzip\t1.13-1\t999\n";
    const auto pkgs = PackageInventory::parseRpm(out);
    assert(pkgs.size() == 2);
    assert(pkgs[0].name == "bash");
    assert(pkgs[0].version == "5.2.21-3");
    assert(pkgs[0].sizeBytes == 1234567); // bytes, not KiB
    assert(pkgs[0].source == "rpm");
    assert(pkgs[1].sizeBytes == 999);
    std::cout << "  parseRpm OK\n";
}

void testParseFlatpak() {
    const std::string out =
        "org.gnome.Calculator\t45.2\t211756\n"
        "org.test.App\t1.0\n"; // no size column
    const auto pkgs = PackageInventory::parseFlatpak(out);
    assert(pkgs.size() == 2);
    assert(pkgs[0].name == "org.gnome.Calculator");
    assert(pkgs[0].version == "45.2");
    assert(pkgs[0].sizeBytes == 211756ull * 1024ull); // KiB -> bytes
    assert(pkgs[0].source == "flatpak");
    assert(pkgs[1].sizeBytes == 0);
    std::cout << "  parseFlatpak OK\n";
}

void testParseSnapList() {
    const std::string withSnaps =
        "Name    Version        Rev    Tracking       Publisher   Notes\n"
        "core22  20240408       1380   22.04           canonical✓  base\n"
        "firefox 128.0+1        4173   latest/stable  mozilla✓    -\n";
    auto pkgs = PackageInventory::parseSnapList(withSnaps);
    assert(pkgs.size() == 2);
    assert(pkgs[0].name == "core22");
    assert(pkgs[0].version == "20240408");
    assert(pkgs[0].source == "snap");
    assert(pkgs[1].name == "firefox");

    assert(PackageInventory::parseSnapList("no snaps installed on this system\n").empty());
    assert(PackageInventory::parseSnapList("").empty());
    std::cout << "  parseSnapList OK\n";
}

void testPacmanLocalDb(const fs::path& root) {
    const fs::path db = root / "pacman-local";
    writeFile(db / "linux-6.1.1-1" / "desc",
              "%NAME%\nlinux\n\n%VERSION%\n6.1.1-1\n\n%SIZE%\n12345678\n\n%REASON%\n1\n");
    writeFile(db / "htop-3.2.2-4" / "desc",
              "%NAME%\ntop\n\n%VERSION%\n3.2.2-4\n\n%SIZE%\n400000\n");
    // A broken entry (no %NAME%) must be skipped, not crash the walk.
    writeFile(db / "broken-1" / "desc", "%VERSION%\n1.0\n");
    writeFile(db / "not-a-package" / "somefile", "ignored\n");

    const auto pkgs = readPacmanLocalDb(db);
    assert(pkgs.size() == 2);
    std::size_t sawLinux = 0;
    std::size_t sawHtop = 0;
    for (const auto& p : pkgs) {
        assert(p.source == "pacman");
        if (p.name == "linux") {
            ++sawLinux;
            assert(p.version == "6.1.1-1");
            assert(p.sizeBytes == 12345678);
        }
        if (p.name == "top") {
            ++sawHtop;
            assert(p.sizeBytes == 400000);
        }
    }
    assert(sawLinux == 1);
    assert(sawHtop == 1);
    assert(readPacmanLocalDb(root / "does-not-exist").empty());
    std::cout << "  pacman local db OK\n";
}

void testSnapBytesOnDisk(const fs::path& root) {
    const fs::path snaps = root / "snapd-snaps";
    writeFile(snaps / "firefox_4173.snap", std::string(1000, 'a'));
    writeFile(snaps / "firefox_4500.snap", std::string(500, 'b'));
    writeFile(snaps / "core22_1380.snap", std::string(300, 'c'));
    writeFile(snaps / "readme.txt", "not a snap\n");

    assert(snapBytesOnDisk("firefox", snaps) == 1500);
    assert(snapBytesOnDisk("core22", snaps) == 300);
    assert(snapBytesOnDisk("missing", snaps) == 0);
    assert(snapBytesOnDisk("fire", snaps) == 0);      // no prefix match
    assert(snapBytesOnDisk("", snaps) == 0);
    assert(snapBytesOnDisk("firefox", root / "nope") == 0);
    std::cout << "  snapBytesOnDisk OK\n";
}

void testRemovalHints() {
    PackageInfo pacman{"htop", "3.2", 100, "pacman"};
    assert(removalHint(pacman) == "sudo pacman -Rns htop");

    PackageInfo flatpak{"org.test.App", "1.0", 100, "flatpak"};
    assert(removalHint(flatpak) == "flatpak uninstall org.test.App");

    PackageInfo snap{"firefox", "128", 100, "snap"};
    assert(removalHint(snap) == "sudo snap remove firefox");

    // Tool-specific variants depend on what is installed here; only check the
    // shape (contains the package name and starts with sudo).
    PackageInfo dpkg{"firefox", "128", 100, "dpkg"};
    const std::string dpkgHint = removalHint(dpkg);
    assert(dpkgHint.find("firefox") != std::string::npos);
    assert(dpkgHint.rfind("sudo ", 0) == 0);

    PackageInfo rpm{"bash", "5.2", 100, "rpm"};
    const std::string rpmHint = removalHint(rpm);
    assert(rpmHint.find("bash") != std::string::npos);
    assert(rpmHint.rfind("sudo ", 0) == 0);

    PackageInfo unknown{"x", "1", 0, "mystery"};
    assert(removalHint(unknown).empty());
    assert(removalHint(PackageInfo{}).empty());
    std::cout << "  removalHint OK\n";
}

void testRunReadOnlyQuery() {
    // Success.
    const auto ok = runReadOnlyQuery("printf 'hello'", 5000ms);
    assert(ok.has_value());
    assert(*ok == "hello");

    // Non-zero exit is a failure (backends signal "not installed" this way).
    assert(!runReadOnlyQuery("exit 3", 5000ms).has_value());

    // Timeout: the child must be killed, not waited out.
    const auto start = std::chrono::steady_clock::now();
    assert(!runReadOnlyQuery("sleep 10", 200ms).has_value());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(elapsed < std::chrono::seconds(3));

    // Output cap: abandoned instead of buffered without bound.
    assert(!runReadOnlyQuery("dd if=/dev/zero bs=1024 count=64 2>/dev/null | tr '\\0' 'a'",
                             5000ms, 1024)
               .has_value());

    // Failure to even spawn (bad shell target) — /bin/sh always exists, so a
    // command that does not: must exit non-zero -> nullopt.
    assert(!runReadOnlyQuery("definitely-not-a-real-command-xyz", 5000ms).has_value());
    std::cout << "  runReadOnlyQuery OK\n";
}

void testFindExecutable() {
    assert(!findExecutable("sh").empty()); // /bin/sh is always on PATH here
    assert(findExecutable("definitely-not-a-real-binary-xyz").empty());
    assert(findExecutable("").empty());
    std::cout << "  findExecutable OK\n";
}

void testBackendsAndInventory(const fs::path& root) {
    PackageInventory::Options opts;
    opts.timeout = 1500ms; // keep the live smoke test snappy even if a
                           // backend (e.g. snap without snapd) hangs

    const auto backends = PackageInventory::availableBackends(opts);
    for (const auto& b : backends) {
        assert(b == "dpkg" || b == "pacman" || b == "rpm" || b == "flatpak" ||
               b == "snap");
    }

    // The snap backend is gated on the snapd socket: without it `snap list`
    // would hang rather than fail.
    PackageInventory::Options noSnap = opts;
    noSnap.snapSocket = root / "no-such-socket";
    const auto backendsNoSnap = PackageInventory::availableBackends(noSnap);
    assert(std::find(backendsNoSnap.begin(), backendsNoSnap.end(), "snap") ==
           backendsNoSnap.end());
    if (!findExecutable("snap").empty()) {
        PackageInventory::Options withSnap = opts;
        withSnap.snapSocket = root / "fake.socket";
        writeFile(withSnap.snapSocket, "socket\n");
        const auto bs = PackageInventory::availableBackends(withSnap);
        assert(std::find(bs.begin(), bs.end(), "snap") != bs.end());
    }

    // Live smoke test: must terminate within the per-backend timeout budget
    // and return size-sorted results, whatever is installed on this machine.
    const auto pkgs = PackageInventory().inventory(opts);
    for (std::size_t i = 1; i < pkgs.size(); ++i) {
        assert(pkgs[i - 1].sizeBytes >= pkgs[i].sizeBytes);
    }
    std::cout << "  backends/inventory smoke OK (" << backends.size()
              << " backends, " << pkgs.size() << " packages)\n";
}

} // namespace

int main() {
    std::cout << "Running package inventory tests...\n";
    const fs::path root =
        fs::temp_directory_path() /
        ("driverse_packages_" + std::to_string(static_cast<long>(::getpid())));
    fs::remove_all(root);

    testParseDpkg();
    testParseRpm();
    testParseFlatpak();
    testParseSnapList();
    testPacmanLocalDb(root);
    testSnapBytesOnDisk(root);
    testRemovalHints();
    testRunReadOnlyQuery();
    testFindExecutable();
    testBackendsAndInventory(root);

    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << "All package inventory tests passed!\n";
    return 0;
}
