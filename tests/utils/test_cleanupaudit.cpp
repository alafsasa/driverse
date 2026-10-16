#include "core/cleanup.h"
#include "utils/audit.h"
#include "utils/cleanupaudit.h"

#include <unistd.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using driverse::core::CleanupCandidate;
using driverse::core::CleanupMode;
using driverse::core::CleanupOptions;
using driverse::core::RiskLevel;
using driverse::utils::AuditAction;
using driverse::utils::makeCleanupBeginEntry;
using driverse::utils::makeCleanupResultEntry;

namespace {

fs::path makeTempDir(const std::string& tag) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("driverse-cleanupaudit-" + tag + "-" + std::to_string(::getpid()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    assert(!ec);
    return dir;
}

void writeFile(const fs::path& path, std::size_t bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << std::string(bytes, 'x');
}

CleanupCandidate makeCandidate(const fs::path& path, RiskLevel risk, bool permanentAllowed,
                               std::uintmax_t alloc) {
    CleanupCandidate candidate;
    candidate.path = path;
    candidate.risk = risk;
    candidate.permanentDeleteAllowed = permanentAllowed;
    candidate.allocatedSize = alloc;
    candidate.logicalSize = alloc;
    return candidate;
}

CleanupOptions optionsFor(const fs::path& base, CleanupMode mode) {
    CleanupOptions options;
    options.mode = mode;
    options.trashRoot = base / "Trash";
    options.currentUserUid = static_cast<std::uintmax_t>(::getuid());
    options.homeDirectory = base / "home";
    return options;
}

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

void testBeginEntry() {
    const fs::path base = makeTempDir("begin");
    const fs::path file = base / "work" / "cache" / "a.bin";
    writeFile(file, 10);

    const auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, 10)}, options);

    const auto entry = makeCleanupBeginEntry(plan, "tester");
    assert(entry.action == AuditAction::Cleanup);
    assert(entry.mode == "apply");
    assert(entry.user == "tester");
    assert(entry.totalBytes == 10);
    assert(entry.items.size() == 1);
    assert(entry.items[0].risk == "Low");
    assert(entry.items[0].result == "planned");
    assert(entry.result.empty()); // result is only known after execution

    const std::string json = driverse::utils::AuditLog::toJson(entry);
    assert(json.find("\"action\":\"cleanup\"") != std::string::npos);
    assert(json.find("\"mode\":\"apply\"") != std::string::npos);

    fs::remove_all(base);
}

void testDryRunEntry() {
    const fs::path base = makeTempDir("dry");
    const fs::path file = base / "work" / "cache" / "b.bin";
    writeFile(file, 20);

    const auto options = optionsFor(base, CleanupMode::DryRun);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Medium, false, 20)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);
    const auto entry = makeCleanupResultEntry(plan, report, "tester");

    assert(entry.action == AuditAction::Preview);
    assert(entry.mode == "dry-run");
    assert(entry.result == "ok");
    assert(entry.totalBytes == 20);
    assert(entry.items.size() == 1);
    assert(entry.items[0].result == "dry-run");
    assert(entry.items[0].risk == "Medium");

    fs::remove_all(base);
}

void testApplyEntries() {
    const fs::path base = makeTempDir("apply");
    const fs::path file = base / "work" / "cache" / "c.bin";
    writeFile(file, 30);

    const auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, 30)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);
    const auto entry = makeCleanupResultEntry(plan, report, "tester");

    assert(entry.action == AuditAction::Cleanup);
    assert(entry.mode == "apply");
    assert(entry.result == "ok");
    assert(entry.totalBytes == 30); // trashed bytes count as affected
    assert(entry.items.size() == 1);
    assert(entry.items[0].result == "trashed");
    assert(entry.items[0].error.empty());
    assert(entry.errors.empty());

    fs::remove_all(base);
}

void testPermanentDeleteEntry() {
    const fs::path base = makeTempDir("perm");
    const fs::path file = base / "work" / "cache" / "d.bin";
    writeFile(file, 40);

    auto options = optionsFor(base, CleanupMode::Apply);
    options.allowPermanentDelete = true;
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Safe, true, 40)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);
    const auto entry = makeCleanupResultEntry(plan, report, "tester");

    assert(entry.result == "ok");
    assert(entry.totalBytes == 40); // freed bytes
    assert(entry.items[0].result == "deleted");
    assert(entry.items[0].risk == "Safe");

    fs::remove_all(base);
}

void testAbortedEntry() {
    const fs::path base = makeTempDir("aborted");
    const fs::path file = base / "work" / "cache" / "e.bin";
    writeFile(file, 50);

    auto options = optionsFor(base, CleanupMode::Apply);
    // Make the trash root unusable: a plain file where the directory should be.
    writeFile(base / "Trash", 1);

    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, 50)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);
    assert(report.aborted);

    const auto entry = makeCleanupResultEntry(plan, report, "tester");
    assert(entry.result == "failed");
    assert(!entry.errors.empty());
    assert(entry.errors[0].find("aborted") != std::string::npos);

    fs::remove_all(base);
}

void testLogRoundTrip() {
    const fs::path base = makeTempDir("log");
    const fs::path logPath = base / "audit.jsonl";
    const fs::path file = base / "work" / "cache" / "f.bin";
    writeFile(file, 60);

    const auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, 60)}, options);

    driverse::utils::AuditLog log(logPath);
    assert(log.append(makeCleanupBeginEntry(plan, "tester")));

    const auto report = driverse::core::executeCleanup(plan, options);
    assert(log.append(makeCleanupResultEntry(plan, report, "tester")));

    const auto lines = readLines(logPath);
    assert(lines.size() == 2);
    assert(lines[0].find("cleanup begin") != std::string::npos);
    assert(lines[0].find("\"ts\":\"") != std::string::npos);
    assert(lines[1].find("\"result\":\"ok\"") != std::string::npos);
    assert(lines[1].find("\"result\":\"trashed\"") != std::string::npos);

    fs::remove_all(base);
}

} // namespace

int main() {
    std::cout << "Running cleanup audit tests...\n";

    testBeginEntry();
    testDryRunEntry();
    testApplyEntries();
    testPermanentDeleteEntry();
    testAbortedEntry();
    testLogRoundTrip();

    std::cout << "All cleanup audit tests passed!\n";
    return 0;
}
