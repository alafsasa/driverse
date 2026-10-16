#include "platform/trash.h"

#include <unistd.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using driverse::platform::Trash;
using driverse::platform::xdgTrashRoot;

namespace {

fs::path makeTempDir(const std::string& tag) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("driverse-trash-test-" + tag + "-" + std::to_string(::getpid()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    assert(!ec);
    return dir;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void testMoveFile() {
    const fs::path base = makeTempDir("file");
    const fs::path trashRoot = base / "Trash";
    const fs::path work = base / "work";
    fs::create_directories(work);

    const fs::path original = work / "hello world.txt";
    {
        std::ofstream out(original);
        out << "payload";
    }

    Trash trash(trashRoot);
    const auto result = trash.move(original);

    assert(result.ok);
    assert(!fs::exists(original));
    assert(fs::exists(result.trashedPath));
    assert(result.trashedPath.parent_path() == trashRoot / "files");
    assert(readFile(result.trashedPath) == "payload");

    // A .trashinfo record must exist with the original path (percent-encoded).
    const fs::path info = trashRoot / "info" / "hello world.txt.trashinfo";
    assert(fs::exists(info));
    const std::string record = readFile(info);
    assert(record.find("[Trash Info]") == 0);
    assert(record.find("Path=" + Trash::encodePath(original)) != std::string::npos);
    assert(record.find("DeletionDate=") != std::string::npos);

    fs::remove_all(base);
}

void testEncodePath() {
    assert(Trash::encodePath("/home/alice/a b.txt") == "/home/alice/a%20b.txt");
    assert(Trash::encodePath("/tmp/plain-1_2.3~x") == "/tmp/plain-1_2.3~x");
    assert(Trash::encodePath("/tmp/caf\xC3\xA9") == "/tmp/caf%C3%A9");
}

void testNameCollision() {
    const fs::path base = makeTempDir("collision");
    const fs::path trashRoot = base / "Trash";

    for (int i = 0; i < 3; ++i) {
        const fs::path dir = base / ("w" + std::to_string(i));
        fs::create_directories(dir);
        const fs::path file = dir / "same.txt";
        std::ofstream(file) << "v" << i;

        Trash trash(trashRoot);
        const auto result = trash.move(file);
        assert(result.ok);
    }

    assert(fs::exists(trashRoot / "files" / "same.txt"));
    assert(fs::exists(trashRoot / "files" / "same.txt.1"));
    assert(fs::exists(trashRoot / "files" / "same.txt.2"));
    fs::remove_all(base);
}

void testMoveDirectory() {
    const fs::path base = makeTempDir("dir");
    const fs::path trashRoot = base / "Trash";
    const fs::path dir = base / "cachedir";
    fs::create_directories(dir / "nested");
    std::ofstream(dir / "nested" / "file.bin") << "data";

    Trash trash(trashRoot);
    const auto result = trash.move(dir);
    assert(result.ok);
    assert(!fs::exists(dir));
    assert(fs::exists(result.trashedPath / "nested" / "file.bin"));
    fs::remove_all(base);
}

void testSymlinkIsNotFollowed() {
    const fs::path base = makeTempDir("symlink");
    const fs::path trashRoot = base / "Trash";
    const fs::path target = base / "target.txt";
    std::ofstream(target) << "important";
    const fs::path link = base / "link.txt";
    fs::create_symlink(target, link);

    Trash trash(trashRoot);
    const auto result = trash.move(link);
    assert(result.ok);

    // The link moved; the target is untouched.
    assert(fs::symlink_status(link).type() == fs::file_type::not_found);
    assert(fs::exists(target));
    assert(fs::is_symlink(result.trashedPath));

    fs::remove_all(base);
}

void testMoveMissingPathFails() {
    const fs::path base = makeTempDir("missing");
    Trash trash(base / "Trash");
    const auto result = trash.move(base / "nope.txt");
    assert(!result.ok);
    assert(!result.error.empty());
    fs::remove_all(base);
}

} // namespace

int main() {
    std::cout << "Running trash tests...\n";

    testEncodePath();
    testMoveFile();
    testNameCollision();
    testMoveDirectory();
    testSymlinkIsNotFollowed();
    testMoveMissingPathFails();

    // Sanity: default root follows XDG.
    const fs::path root = xdgTrashRoot();
    assert(root.filename() == "Trash");

    std::cout << "All trash tests passed!\n";
    return 0;
}
