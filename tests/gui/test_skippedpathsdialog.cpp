#include "gui/skippedpathsdialog.h"

#include <QApplication>

#include <cassert>
#include <iostream>
#include <vector>

using driverse::core::SkipReason;
using driverse::core::SkippedPath;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::cout << "Running skipped paths dialog tests...\n";

    const std::vector<SkippedPath> skipped{
        {"/proc", SkipReason::ProtectedPath, "Protected system path"},
        {"/home/user/secret", SkipReason::PermissionDenied, "Permission denied"},
        {"/mnt/loop", SkipReason::LoopDetected, ""}, // reasonString empty: fall back to enum
    };
    driverse::gui::SkippedPathsDialog dialog(skipped, 42);

    assert(dialog.rowCount() == 3);
    assert(dialog.pathAt(0) == QStringLiteral("/proc"));
    assert(dialog.reasonAt(0) == QStringLiteral("Protected system path"));
    assert(dialog.reasonAt(1) == QStringLiteral("Permission denied"));
    assert(dialog.reasonAt(2).contains(QStringLiteral("cycle")));

    // Out-of-range access must be safe.
    assert(dialog.pathAt(99).isEmpty());
    assert(dialog.pathAt(-1).isEmpty());
    assert(dialog.reasonAt(99).isEmpty());

    // Empty list: no rows, no crash.
    driverse::gui::SkippedPathsDialog empty({}, 0);
    assert(empty.rowCount() == 0);

    std::cout << "All skipped paths dialog tests passed!\n";
    return 0;
}
