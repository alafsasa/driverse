#include "utils/config.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace driverse::utils;
namespace fs = std::filesystem;

namespace {

fs::path tempConfPath() {
    return fs::temp_directory_path() / ("driverse_conf_" + std::to_string(::getpid())) /
           "driverse.conf";
}

void testDefaults() {
    Config c = Config::defaults();
    assert(c.getInt("general.min_age_safe_days") == 7);
    assert(c.getInt("general.min_age_temp_days") == 3);
    assert(c.getInt("general.min_age_downloads_days") == 90);
    assert(c.getBool("general.allow_permanent_delete") == true);

    auto prot = c.protectedPaths();
    assert(!prot.empty());

    auto excl = c.excludedPaths();
    assert(!excl.empty());

    auto fsTypes = c.excludedFilesystemTypes();
    assert(!fsTypes.empty());

    // Unknown keys fall back to provided defaults.
    assert(c.getString("nope.key", "fallback") == "fallback");
    assert(c.getInt("nope.key", 42) == 42);
    assert(c.getBool("nope.key", true) == true);
    assert(c.getStringList("nope.key").empty());
    std::cout << "  defaults OK\n";
}

void testRoundTrip() {
    const fs::path path = tempConfPath();
    fs::remove_all(path.parent_path());

    Config c = Config::defaults();
    c.setInt("general.min_age_safe_days", 14);
    c.setString("general.theme", "dark");
    c.setBool("general.allow_permanent_delete", false);
    c.setStringList("protected.paths", {"/boot", "/etc"});

    assert(c.save(path));
    assert(fs::exists(path));

    Config loaded = Config::load(path);
    assert(loaded.getInt("general.min_age_safe_days") == 14);
    assert(loaded.getString("general.theme") == "dark");
    assert(loaded.getBool("general.allow_permanent_delete") == false);

    auto prot = loaded.protectedPaths();
    assert(prot.size() == 2);
    assert(prot[0] == "/boot");
    assert(prot[1] == "/etc");

    fs::remove_all(path.parent_path());
    std::cout << "  save/load round-trip OK\n";
}

void testMissingFileGivesDefaults() {
    const fs::path path = tempConfPath();
    fs::remove_all(path.parent_path());
    Config c = Config::load(path); // does not exist
    assert(c.getInt("general.min_age_safe_days") == 7);
    std::cout << "  missing file -> defaults OK\n";
}

void testMalformedLinesIgnored() {
    const fs::path path = tempConfPath();
    fs::remove_all(path.parent_path());
    fs::create_directories(path.parent_path());

    {
        std::ofstream out(path);
        out << "# a comment\n";
        out << "; another comment\n";
        out << "garbage-without-equals\n";
        out << "[general]\n";
        out << "min_age_safe_days = 21\n";
        out << "  \n";
        out << "[custom]\n";
        out << "flag = yes\n";
    }

    Config c = Config::load(path);
    assert(c.getInt("general.min_age_safe_days") == 21);
    assert(c.getBool("custom.flag") == true);
    // Unparseable line contributes nothing.
    assert(!c.has("general.garbage-without-equals"));

    fs::remove_all(path.parent_path());
    std::cout << "  malformed lines ignored OK\n";
}

void testBadTypesFallBack() {
    Config c = Config::defaults();
    c.setString("general.num", "not-a-number");
    assert(c.getInt("general.num", -1) == -1); // parse failure -> default
    c.setString("general.flag", "maybe");
    assert(c.getBool("general.flag", false) == false);
    std::cout << "  bad type values fall back OK\n";
}

void testDefaultPathUsesXdg() {
#if defined(__unix__)
    ::setenv("XDG_CONFIG_HOME", "/tmp/xdgtest", 1);
    assert(defaultConfigPath() == fs::path("/tmp/xdgtest/driverse/driverse.conf"));
    ::unsetenv("XDG_CONFIG_HOME");
#endif
    std::cout << "  defaultConfigPath XDG OK\n";
}

} // namespace

int main() {
    std::cout << "Running config tests...\n";
    testDefaults();
    testRoundTrip();
    testMissingFileGivesDefaults();
    testMalformedLinesIgnored();
    testBadTypesFallBack();
    testDefaultPathUsesXdg();
    std::cout << "All config tests passed!\n";
    return 0;
}
