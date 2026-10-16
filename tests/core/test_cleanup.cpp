#include "core/cleanup.h"
#include "core/protections.h"
#include "core/utils.h"

#include <unistd.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using driverse::core::CleanupAction;
using driverse::core::CleanupCandidate;
using driverse::core::CleanupMode;
using driverse::core::CleanupOptions;
using driverse::core::CleanupStatus;
using driverse::core::RiskLevel;

namespace {

fs::path makeTempDir(const std::string& tag) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("driverse-cleanup-test-" + tag + "-" + std::to_string(::getpid()));
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

CleanupCandidate makeCandidate(const fs::path& path, RiskLevel risk, bool isDir,
                               bool permanentAllowed = false, std::uintmax_t alloc = 0) {
    CleanupCandidate candidate;
    candidate.path = path;
    candidate.risk = risk;
    candidate.isDirectory = isDir;
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

void testPlanDecisions() {
    const fs::path base = makeTempDir("plan");
    const auto options = optionsFor(base, CleanupMode::DryRun);

    const fs::path cache = base / "work" / "cache" / "a.bin";
    const fs::path downloads = base / "work" / "Downloads" / "b.bin";
    writeFile(cache, 100);
    writeFile(downloads, 50);

    std::vector<CleanupCandidate> candidates{
        makeCandidate(cache, RiskLevel::Low, false, false, 100),
        makeCandidate(downloads, RiskLevel::Safe, false, true, 50),
        // System-protected is never actionable.
        makeCandidate(base / "work" / "sys", RiskLevel::SystemProtected, true, false, 999),
        // Missing path.
        makeCandidate(base / "work" / "ghost", RiskLevel::Low, false, false, 42),
    };

    const auto plan = driverse::core::buildCleanupPlan(candidates, options);

    assert(plan.items.size() == 4);
    assert(plan.actionableCount == 2);
    assert(plan.skippedCount == 2);
    assert(plan.totalBytes == 150);
    assert(plan.countByRisk[static_cast<int>(RiskLevel::Low)] == 1);
    assert(plan.countByRisk[static_cast<int>(RiskLevel::Safe)] == 1);
    assert(plan.bytesByRisk[static_cast<int>(RiskLevel::Low)] == 100);

    assert(plan.items[0].action == CleanupAction::MoveToTrash);
    // Without the opt-in, an eligible cache is still trashed (never deleted).
    assert(plan.items[1].action == CleanupAction::MoveToTrash);
    assert(plan.items[2].action == CleanupAction::Skip);
    assert(plan.items[2].skipReason == "system-protected");
    assert(plan.items[3].action == CleanupAction::Skip);
    assert(plan.items[3].skipReason == "no longer exists");

    fs::remove_all(base);
}

void testPermanentDeleteRequiresOptIn() {
    const fs::path base = makeTempDir("perm");
    const fs::path cache = base / "work" / "cache" / "c.bin";
    writeFile(cache, 64);
    const std::vector<CleanupCandidate> candidates{
        makeCandidate(cache, RiskLevel::Safe, false, true, 64)};

    {
        auto options = optionsFor(base, CleanupMode::Apply);
        options.allowPermanentDelete = false;
        const auto plan = driverse::core::buildCleanupPlan(candidates, options);
        assert(plan.items[0].action == CleanupAction::MoveToTrash);
    }

    auto options = optionsFor(base, CleanupMode::Apply);
    options.allowPermanentDelete = true;
    const auto plan = driverse::core::buildCleanupPlan(candidates, options);
    assert(plan.items[0].action == CleanupAction::PermanentDelete);

    const auto report = driverse::core::executeCleanup(plan, options);
    assert(report.ok());
    assert(report.deleted == 1);
    assert(report.bytesFreed == 64);
    assert(!fs::exists(cache));

    fs::remove_all(base);
}

void testOwnershipRule() {
    const fs::path base = makeTempDir("owner");
    const fs::path file = base / "work" / "d.bin";
    writeFile(file, 10);

    auto options = optionsFor(base, CleanupMode::DryRun);
    options.currentUserUid = static_cast<std::uintmax_t>(::getuid()) + 100000; // "somebody else"

    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 10)}, options);
    assert(plan.skippedCount == 1);
    assert(plan.items[0].skipReason.find("not owned") != std::string::npos);
    assert(plan.actionableCount == 0);

    fs::remove_all(base);
}

void testDangerousRoots() {
    const fs::path base = makeTempDir("guard");
    auto options = optionsFor(base, CleanupMode::DryRun);

    // "/" is never actionable.
    const auto rootPlan = driverse::core::buildCleanupPlan(
        {makeCandidate("/", RiskLevel::Low, true, false, 1)}, options);
    assert(rootPlan.skippedCount == 1);

    // The home directory itself and any ancestor of it are refused.
    const auto homePlan = driverse::core::buildCleanupPlan(
        {makeCandidate(base / "home", RiskLevel::Low, true, false, 1)}, options);
    assert(homePlan.skippedCount == 1);

    const auto ancestorPlan = driverse::core::buildCleanupPlan(
        {makeCandidate(base, RiskLevel::Low, true, false, 1)}, options);
    assert(ancestorPlan.skippedCount == 1);

    fs::remove_all(base);
}

void testDryRunTouchesNothing() {
    const fs::path base = makeTempDir("dry");
    const fs::path file = base / "work" / "cache" / "e.bin";
    writeFile(file, 128);

    const auto options = optionsFor(base, CleanupMode::DryRun);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 128)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);

    assert(report.ok());
    assert(report.dryRun == 1);
    assert(report.bytesFreed == 0);
    assert(report.bytesTrashed == 0);
    assert(report.items[0].status == CleanupStatus::DryRun);
    assert(fs::exists(file));
    assert(!fs::exists(base / "Trash"));

    fs::remove_all(base);
}

void testApplyMovesToTrash() {
    const fs::path base = makeTempDir("apply");
    const fs::path file = base / "work" / "cache" / "f.bin";
    writeFile(file, 200);

    const auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 200)}, options);
    const auto report = driverse::core::executeCleanup(plan, options);

    assert(report.ok());
    assert(report.moved == 1);
    assert(report.bytesTrashed == 200);
    assert(report.bytesFreed == 0); // trash is recoverable, not reclaimed
    assert(!fs::exists(file));
    assert(fs::exists(base / "Trash" / "files" / "f.bin"));
    assert(fs::exists(base / "Trash" / "info" / "f.bin.trashinfo"));
    assert(report.items[0].status == CleanupStatus::MovedToTrash);

    fs::remove_all(base);
}

void testToctouAbortsOnSubstitution() {
    const fs::path base = makeTempDir("toctou");
    const fs::path file = base / "work" / "cache" / "g.bin";
    writeFile(file, 20);

    auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 20)}, options);
    assert(plan.actionableCount == 1);

    // Substitute the file with a different inode (the old one still exists).
    fs::rename(file, file.string() + ".moved");
    writeFile(file, 99);

    const auto report = driverse::core::executeCleanup(plan, options);
    assert(report.aborted);
    assert(report.abortReason.find("identity changed") != std::string::npos);
    assert(report.moved == 0);
    assert(fs::exists(file)); // the new file is untouched
    assert(!fs::exists(base / "Trash" / "files" / "g.bin"));

    fs::remove_all(base);
}

void testToctouSkipsWhenConfigured() {
    const fs::path base = makeTempDir("toctou-skip");
    const fs::path file = base / "work" / "cache" / "h.bin";
    writeFile(file, 20);

    auto options = optionsFor(base, CleanupMode::Apply);
    options.abortOnDrift = false;
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 20)}, options);

    fs::rename(file, file.string() + ".moved");
    writeFile(file, 30);

    const auto report = driverse::core::executeCleanup(plan, options);
    assert(!report.aborted);
    assert(report.skipped == 1);
    assert(report.moved == 0);
    assert(fs::exists(file));

    fs::remove_all(base);
}

void testProtectedRecheckAtExecution() {
    const fs::path base = makeTempDir("protect");
    const fs::path file = base / "work" / "cache" / "i.bin";
    writeFile(file, 12);

    auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(file, RiskLevel::Low, false, false, 12)}, options);
    assert(plan.actionableCount == 1);

    // The path becomes protected between preview and execution.
    const fs::path canonical = driverse::core::utils::canonicalizeNoFollow(file);
    driverse::core::Protections::instance().addProtectedPath(canonical);
    const auto report = driverse::core::executeCleanup(plan, options);
    driverse::core::Protections::instance().removeProtectedPath(canonical);

    assert(report.skipped == 1);
    assert(report.items[0].detail.find("protected") != std::string::npos);
    assert(fs::exists(file));

    fs::remove_all(base);
}

void testDirectoriesAndStatusStrings() {
    const fs::path base = makeTempDir("dirs");
    const fs::path dir = base / "work" / "cache" / "thumbnails";
    writeFile(dir / "nested" / "t.jpg", 40);

    const auto options = optionsFor(base, CleanupMode::Apply);
    const auto plan = driverse::core::buildCleanupPlan(
        {makeCandidate(dir, RiskLevel::Safe, true, true, 40)}, options);
    assert(plan.items[0].action == CleanupAction::MoveToTrash);

    const auto report = driverse::core::executeCleanup(plan, options);
    assert(report.ok());
    assert(report.moved == 1);
    assert(!fs::exists(dir));
    assert(fs::exists(base / "Trash" / "files" / "thumbnails" / "nested" / "t.jpg"));

    assert(driverse::core::cleanupStatusToString(CleanupStatus::MovedToTrash) == "trashed");
    assert(driverse::core::cleanupStatusToString(CleanupStatus::Deleted) == "deleted");
    assert(driverse::core::cleanupActionToString(CleanupAction::PermanentDelete) ==
           "permanently delete");

    fs::remove_all(base);
}

} // namespace

int main() {
    std::cout << "Running cleanup tests...\n";

    testPlanDecisions();
    testPermanentDeleteRequiresOptIn();
    testOwnershipRule();
    testDangerousRoots();
    testDryRunTouchesNothing();
    testApplyMovesToTrash();
    testToctouAbortsOnSubstitution();
    testToctouSkipsWhenConfigured();
    testProtectedRecheckAtExecution();
    testDirectoriesAndStatusStrings();

    std::cout << "All cleanup tests passed!\n";
    return 0;
}
