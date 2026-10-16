// Phase 10 GUI tests: risk-based confirmation gates and the cleanup dialog's
// gating behaviour. Runs headless with QT_QPA_PLATFORM=offscreen.
#include "gui/cleanupdialog.h"

#include <unistd.h>

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using driverse::core::CleanupAction;
using driverse::core::CleanupCandidate;
using driverse::core::CleanupItem;
using driverse::core::CleanupPlan;
using driverse::core::RiskLevel;
using driverse::gui::applyEnabled;
using driverse::gui::CleanupConfirmation;
using driverse::gui::CleanupDialog;
using driverse::gui::permanentDeleteOffered;
using driverse::gui::planHasActionableRisk;
using driverse::gui::typeConfirmationRequired;

namespace {

fs::path makeTempDir(const std::string& tag) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) /
                         ("driverse-dialog-test-" + tag + "-" + std::to_string(::getpid()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    assert(!ec);
    return dir;
}

void writeFile(const fs::path& path, std::size_t bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    assert(out.good());
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

CleanupItem makeItem(RiskLevel risk, CleanupAction action, bool permanentAllowed = false,
                     std::uintmax_t bytes = 0) {
    CleanupItem item;
    item.path = "/tmp/driverse-fake";
    item.risk = risk;
    item.action = action;
    item.permanentDeleteAllowed = permanentAllowed;
    item.allocatedSize = bytes;
    return item;
}

CleanupPlan makePlan(std::vector<CleanupItem> items) {
    CleanupPlan plan;
    for (const auto& item : items) {
        if (item.action != CleanupAction::Skip) {
            ++plan.actionableCount;
            plan.totalBytes += item.allocatedSize;
        }
    }
    plan.items = std::move(items);
    return plan;
}

// ---- Pure gate functions ----

void testGatesPure() {
    const auto lowPlan = makePlan({makeItem(RiskLevel::Low, CleanupAction::MoveToTrash, false, 10)});
    assert(!planHasActionableRisk(lowPlan, RiskLevel::High));
    assert(planHasActionableRisk(lowPlan, RiskLevel::Low));
    assert(!permanentDeleteOffered(lowPlan));

    CleanupConfirmation confirmation;
    // Nothing acknowledged -> not allowed.
    assert(!applyEnabled(lowPlan, confirmation));

    confirmation.actionAcknowledged = true;
    assert(applyEnabled(lowPlan, confirmation));
    assert(!typeConfirmationRequired(lowPlan, confirmation));

    // Skipped items never make a plan "high risk".
    const auto skippedHigh = makePlan({makeItem(RiskLevel::High, CleanupAction::Skip)});
    assert(!planHasActionableRisk(skippedHigh, RiskLevel::High));
    assert(!applyEnabled(skippedHigh, CleanupConfirmation{})); // nothing actionable

    // High risk: needs review + typed confirmation.
    const auto highPlan = makePlan({makeItem(RiskLevel::High, CleanupAction::MoveToTrash, false, 10)});
    assert(planHasActionableRisk(highPlan, RiskLevel::High));
    assert(!applyEnabled(highPlan, confirmation));
    confirmation.highRiskReviewed = true;
    assert(typeConfirmationRequired(highPlan, confirmation));
    assert(!applyEnabled(highPlan, confirmation));
    confirmation.typedConfirmation = QStringLiteral("DELETE");
    assert(applyEnabled(highPlan, confirmation));
    // Wrong word stays blocked.
    confirmation.typedConfirmation = QStringLiteral("delete");
    assert(!applyEnabled(highPlan, confirmation));

    // Permanent delete opt-in: eligible Safe caches are offered.
    const auto permPlan =
        makePlan({makeItem(RiskLevel::Safe, CleanupAction::MoveToTrash, true, 10)});
    assert(permanentDeleteOffered(permPlan));
    CleanupConfirmation permConfirmation;
    permConfirmation.actionAcknowledged = true;
    assert(applyEnabled(permPlan, permConfirmation)); // trash-only run is fine
    permConfirmation.permanentDelete = true;
    assert(typeConfirmationRequired(permPlan, permConfirmation));
    assert(!applyEnabled(permPlan, permConfirmation));
    permConfirmation.typedConfirmation = QStringLiteral("DELETE");
    assert(applyEnabled(permPlan, permConfirmation));

    // Safe cache that is not marked regenerable is never offered.
    const auto notOffered =
        makePlan({makeItem(RiskLevel::Safe, CleanupAction::MoveToTrash, false, 10)});
    assert(!permanentDeleteOffered(notOffered));
}

// ---- Dialog behaviour ----

struct Widgets {
    QPushButton* apply = nullptr;
    QPushButton* dryRun = nullptr;
    QCheckBox* confirmAction = nullptr;
    QCheckBox* confirmHigh = nullptr;
    QCheckBox* confirmPermanent = nullptr;
    QLineEdit* typeConfirm = nullptr;
};

Widgets findWidgets(CleanupDialog& dialog) {
    Widgets w;
    w.apply = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
    w.dryRun = dialog.findChild<QPushButton*>(QStringLiteral("dryRunButton"));
    w.confirmAction = dialog.findChild<QCheckBox*>(QStringLiteral("confirmAction"));
    w.confirmHigh = dialog.findChild<QCheckBox*>(QStringLiteral("confirmHigh"));
    w.confirmPermanent = dialog.findChild<QCheckBox*>(QStringLiteral("confirmPermanent"));
    w.typeConfirm = dialog.findChild<QLineEdit*>(QStringLiteral("typeConfirm"));
    assert(w.apply && w.dryRun && w.confirmAction && w.confirmHigh && w.confirmPermanent &&
           w.typeConfirm);
    return w;
}

void testDialogLowRisk() {
    const fs::path base = makeTempDir("low");
    const fs::path file = base / "work" / "cache" / "low.bin";
    writeFile(file, 64);

    CleanupDialog dialog(
        {makeCandidate(file, RiskLevel::Low, false, false, 64)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);

    assert(!dialog.applied());
    assert(w.dryRun->isEnabled());
    assert(!w.apply->isEnabled());                       // nothing acknowledged yet
    assert(!w.confirmHigh->isVisible());                 // no High risk items
    assert(!w.confirmPermanent->isVisible());            // nothing eligible for deletion

    w.confirmAction->setChecked(true);
    assert(w.apply->isEnabled());                        // Low risk needs no extra gate

    dialog.close();
    fs::remove_all(base);
}

void testDialogHighRisk() {
    const fs::path base = makeTempDir("high");
    const fs::path file = base / "work" / "Downloads" / "video.mkv";
    writeFile(file, 32);

    CleanupDialog dialog(
        {makeCandidate(file, RiskLevel::High, false, false, 32)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);

    assert(w.confirmHigh->isVisible());
    w.confirmAction->setChecked(true);
    assert(!w.apply->isEnabled()); // High risk review still missing

    w.confirmHigh->setChecked(true);
    assert(!w.apply->isEnabled()); // typed confirmation still missing

    w.typeConfirm->setText(QStringLiteral("DELETE"));
    assert(w.apply->isEnabled());

    dialog.close();
    fs::remove_all(base);
}

void testDialogPermanentDeleteOptIn() {
    const fs::path base = makeTempDir("perm");
    const fs::path file = base / "work" / "cache" / "safe.bin";
    writeFile(file, 128);

    CleanupDialog dialog(
        {makeCandidate(file, RiskLevel::Safe, false, true, 128)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);

    // Eligible for permanent deletion -> the opt-in is offered but off.
    assert(w.confirmPermanent->isVisible());
    assert(w.confirmPermanent->isEnabled());
    assert(!w.confirmPermanent->isChecked());

    w.confirmAction->setChecked(true);
    assert(w.apply->isEnabled()); // default plan trashes everything

    w.confirmPermanent->setChecked(true);
    assert(!w.apply->isEnabled()); // now needs the typed confirmation
    w.typeConfirm->setText(QStringLiteral("DELETE"));
    assert(w.apply->isEnabled());

    dialog.close();
    fs::remove_all(base);
}

void testDialogSkipsProtectedAndMissing() {
    const fs::path base = makeTempDir("skips");
    // A candidate that no longer exists is skipped at plan time, leaving the
    // plan with nothing actionable -> Apply must stay disabled forever.
    CleanupDialog dialog(
        {makeCandidate(base / "gone.bin", RiskLevel::Low, false, false, 10)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);

    w.confirmAction->setChecked(true);
    assert(!w.apply->isEnabled());
    assert(!w.dryRun->isEnabled());

    dialog.close();
    fs::remove_all(base);
}

// ---- End-to-end apply (audit log + trash are redirected to a temp dir) ----

fs::path dataHome() {
    return fs::path(qgetenv("XDG_DATA_HOME").constData());
}

void testApplyEndToEnd() {
    const fs::path base = makeTempDir("e2e");
    const fs::path file = base / "work" / "cache" / "e2e.bin";
    writeFile(file, 256);

    CleanupDialog dialog(
        {makeCandidate(file, RiskLevel::Low, false, false, 256)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);
    w.confirmAction->setChecked(true);
    assert(w.apply->isEnabled());

    dialog.applyConfirmed();

    assert(dialog.applied());
    assert(!fs::exists(file)); // moved away
    assert(fs::exists(dataHome() / "Trash" / "files" / "e2e.bin")); // recoverable
    assert(fs::exists(dataHome() / "Trash" / "info" / "e2e.bin.trashinfo"));

    // The audit log must contain the "before" and "after" records.
    const fs::path logPath = dataHome() / "driverse" / "audit.jsonl";
    std::ifstream in(logPath);
    std::string all;
    std::string line;
    while (std::getline(in, line)) {
        all += line + "\n";
    }
    assert(all.find("cleanup begin") != std::string::npos);
    assert(all.find("\"mode\":\"apply\"") != std::string::npos);
    assert(all.find("\"result\":\"trashed\"") != std::string::npos);
    assert(all.find("\"result\":\"ok\"") != std::string::npos);

    dialog.close();
    fs::remove_all(base);
}

void testApplyRefusedWhenAuditUnwritable() {
    const fs::path base = makeTempDir("audit-gate");
    const fs::path file = base / "work" / "cache" / "gate.bin";
    writeFile(file, 32);

    // Break the audit log path: a plain file where the log directory should be.
    std::error_code cleanupEc;
    fs::remove_all(dataHome() / "driverse", cleanupEc);
    writeFile(dataHome() / "driverse", 1);

    CleanupDialog dialog(
        {makeCandidate(file, RiskLevel::Low, false, false, 32)}, nullptr);
    dialog.show();
    const Widgets w = findWidgets(dialog);
    w.confirmAction->setChecked(true);

    dialog.applyConfirmed();
    assert(!dialog.applied());
    assert(fs::exists(file)); // nothing was modified without an audit record

    // Repair the path: the same apply now goes through.
    std::error_code ec;
    fs::remove(dataHome() / "driverse", ec);
    dialog.applyConfirmed();
    assert(dialog.applied());
    assert(!fs::exists(file));

    dialog.close();
    fs::remove_all(base);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the trash and audit log away from the real home directory.
    const fs::path dataHome = makeTempDir("datahome");
    qputenv("XDG_DATA_HOME", QByteArray::fromStdString(dataHome.string()));
    QApplication app(argc, argv);

    std::cout << "Running cleanup dialog tests...\n";

    testGatesPure();
    testDialogLowRisk();
    testDialogHighRisk();
    testDialogPermanentDeleteOptIn();
    testDialogSkipsProtectedAndMissing();
    testApplyEndToEnd();
    testApplyRefusedWhenAuditUnwritable();

    std::cout << "All cleanup dialog tests passed!\n";
    return 0;
}
