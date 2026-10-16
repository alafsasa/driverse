#pragma once

#include "core/types.h"

#include <QDialog>
#include <QString>

#include <cstdint>
#include <vector>

class QLabel;
class QTableWidget;

namespace driverse::gui {

// Read-only list of paths the scanner did not descend into, with the reason for
// each (permission denied, protected system path, filesystem cycle, ...).
// Purely informational; it never offers any action on the paths.
class SkippedPathsDialog : public QDialog {
    Q_OBJECT

public:
    // `skipped` is the (capped) recorded list; `totalSkipped` is the scanner's
    // total count so the dialog can say when entries are not shown.
    SkippedPathsDialog(std::vector<core::SkippedPath> skipped,
                       std::uintmax_t totalSkipped, QWidget* parent = nullptr);

    // Introspection for tests.
    int rowCount() const;
    QString pathAt(int row) const;
    QString reasonAt(int row) const;

private:
    QTableWidget* m_table = nullptr;
};

} // namespace driverse::gui
