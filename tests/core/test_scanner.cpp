#include "core/scanner.h"
#include "core/utils.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

using namespace driverse::core;
namespace fs = std::filesystem;

namespace {

fs::path g_root;

void writeFile(const fs::path& p, std::size_t bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    std::string chunk(1024, 'x');
    std::size_t written = 0;
    while (written < bytes) {
        std::size_t n = std::min<std::size_t>(chunk.size(), bytes - written);
        out.write(chunk.data(), static_cast<std::streamsize>(n));
        written += n;
    }
}

void setUp() {
    g_root = fs::temp_directory_path() / ("driverse_test_" + std::to_string(::getpid()));
    fs::remove_all(g_root);
    fs::create_directories(g_root);

    // Small tree
    writeFile(g_root / "a.txt", 100);
    writeFile(g_root / "sub" / "b.bin", 4096);
    writeFile(g_root / "sub" / "deep" / "c.bin", 8192);

    // A directory we can exclude
    writeFile(g_root / "excluded" / "secret.bin", 65536);

    // Symlink: must not be followed
    fs::create_symlink(g_root / "sub", g_root / "link_to_sub");
}

void tearDown() {
    fs::remove_all(g_root);
}

std::uintmax_t fileAllocated(const fs::path& p) {
    struct stat st {};
    ::stat(p.c_str(), &st);
    return static_cast<std::uintmax_t>(st.st_blocks) * 512;
}

void testBasicAggregation() {
    ScanOptions opt;
    opt.root = g_root;
    opt.skipProtectedPaths = false; // test fixture, keep everything

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    assert(r.error.empty());
    assert(r.completed);
    assert(!r.cancelled);
    assert(r.stats.filesScanned >= 4);
    assert(r.stats.totalLogicalBytes >= 100 + 4096 + 8192 + 65536);
    assert(!r.directories.empty());

    // Symlink counted but not followed
    assert(r.stats.symlinksSkipped == 1);

    std::cout << "  basic aggregation OK (files=" << r.stats.filesScanned
              << ", dirs=" << r.stats.dirsScanned
              << ", logical=" << utils::formatBytes(r.stats.totalLogicalBytes)
              << ", allocated=" << utils::formatBytes(r.stats.totalAllocatedBytes) << ")\n";
}

void testHardLinkDedup() {
    fs::path hlDir = g_root / "hardlinks";
    fs::create_directories(hlDir);
    writeFile(hlDir / "orig.bin", 32768);
    fs::create_hard_link(hlDir / "orig.bin", hlDir / "link.bin");

    ScanOptions opt;
    opt.root = hlDir;
    opt.skipProtectedPaths = false;

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    assert(r.stats.filesScanned == 2);
    assert(r.stats.hardLinksDeduplicated == 1);

    std::uintmax_t oneCopy = fileAllocated(hlDir / "orig.bin");
    // totalAllocated counts both names; unique counts only one physical copy.
    assert(r.stats.totalAllocatedBytes == 2 * oneCopy);
    assert(r.stats.uniqueAllocatedBytes == oneCopy);

    std::cout << "  hard-link dedup OK (total=" << utils::formatBytes(r.stats.totalAllocatedBytes)
              << ", unique=" << utils::formatBytes(r.stats.uniqueAllocatedBytes) << ")\n";
}

void testSparseDetection() {
    fs::path sparseDir = g_root / "sparse";
    fs::create_directories(sparseDir);
    fs::path sparse = sparseDir / "hole.bin";
    std::ofstream out(sparse, std::ios::binary);
    out.seekp(1024 * 1024 - 1);
    out.put('\0');
    out.close();

    ScanOptions opt;
    opt.root = sparseDir;
    opt.skipProtectedPaths = false;

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    bool found = false;
    for (const auto& f : r.largestFiles) {
        if (f.path.filename() == "hole.bin") {
            found = true;
            assert(f.logicalSize >= 1024 * 1024);
            assert(f.isSparse);
        }
    }
    assert(found);
    std::cout << "  sparse detection OK\n";
}

void testExcludePaths() {
    ScanOptions opt;
    opt.root = g_root;
    opt.skipProtectedPaths = false;
    opt.excludePaths = {g_root / "excluded"};

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    for (const auto& d : r.directories) {
        assert(d.path != (g_root / "excluded"));
        assert(d.path != (g_root / "excluded" / "secret.bin"));
    }
    for (const auto& f : r.largestFiles) {
        assert(f.path.string().find("/excluded/") == std::string::npos);
    }
    std::cout << "  exclude paths OK\n";
}

void testTopNRetention() {
    fs::path sizes = g_root / "sizes";
    fs::create_directories(sizes);
    writeFile(sizes / "small.bin", 1024);
    writeFile(sizes / "medium.bin", 16384);
    writeFile(sizes / "large.bin", 131072);

    ScanOptions opt;
    opt.root = sizes;
    opt.skipProtectedPaths = false;
    opt.maxRetainedFiles = 2;

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    assert(r.largestFiles.size() == 2);
    assert(r.largestFiles[0].allocatedSize >= r.largestFiles[1].allocatedSize);
    assert(r.largestFiles[0].path.filename() == "large.bin");
    assert(r.largestFiles[1].path.filename() == "medium.bin");
    std::cout << "  top-N retention OK\n";
}

void testCancellation() {
    ScanOptions opt;
    opt.root = g_root;
    opt.skipProtectedPaths = false;

    Scanner scanner;
    scanner.setOptions(opt);

    Scanner* sptr = &scanner;
    int count = 0;
    scanner.setItemCallback([sptr, &count](const FileItem&) {
        if (++count >= 2) {
            sptr->cancel();
        }
    });

    ScanResult r = scanner.scan();
    assert(r.cancelled);
    assert(!r.completed);
    std::cout << "  cancellation OK\n";
}

void testProtectedSkip() {
    // Scanning "/" with protected skipping must never descend into /sys or /proc.
    // Keep it cheap: limit depth so this stays fast.
    ScanOptions opt;
    opt.root = "/";
    opt.skipProtectedPaths = true;
    opt.maxDepth = 1;

    Scanner scanner;
    scanner.setOptions(opt);
    ScanResult r = scanner.scan();

    for (const auto& d : r.directories) {
        std::string p = d.path.string();
        assert(p != "/proc");
        assert(p != "/sys");
        assert(p != "/dev");
        assert(p != "/run");
    }
    std::cout << "  protected skip OK\n";
}

} // namespace

int main() {
    std::cout << "Running scanner tests...\n";
    setUp();
    try {
        testBasicAggregation();
        testHardLinkDedup();
        testSparseDetection();
        testExcludePaths();
        testTopNRetention();
        testCancellation();
        testProtectedSkip();
    } catch (...) {
        tearDown();
        throw;
    }
    tearDown();
    std::cout << "All scanner tests passed!\n";
    return 0;
}
