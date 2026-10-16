#include "skippedpathsdialog.h"

#include "core/utils.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace driverse::gui {

namespace {

// The scanner can record up to 50k skips; showing all of them helps nobody.
constexpr int kMaxShownRows = 2000;

QString reasonText(const core::SkippedPath& skip) {
    if (!skip.reasonString.empty()) {
        return QString::fromStdString(skip.reasonString);
    }
    return QString::fromStdString(core::utils::skipReasonToString(skip.reason));
}

} // namespace

SkippedPathsDialog::SkippedPathsDialog(std::vector<core::SkippedPath> skipped,
                                       std::uintmax_t totalSkipped, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Skipped paths"));
    setModal(true);
    resize(860, 560);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("These paths were <b>not</b> scanned. Skipping protected system directories "
           "and pseudo-filesystems is intentional; permission errors and filesystem "
           "cycles are reported for transparency. Nothing here is modified."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Per-reason breakdown, most frequent first.
    QMap<QString, int> byReason;
    for (const auto& skip : skipped) {
        ++byReason[reasonText(skip)];
    }
    QStringList parts;
    for (auto it = byReason.constBegin(); it != byReason.constEnd(); ++it) {
        parts << tr("%1: %2").arg(it.key()).arg(it.value());
    }
    auto* summary = new QLabel(
        tr("<b>%1</b> skipped entr%2 recorded. %3")
            .arg(skipped.size())
            .arg(skipped.size() == 1 ? tr("y") : tr("ies"))
            .arg(parts.join(QStringLiteral(" · "))),
        this);
    summary->setWordWrap(true);
    summary->setObjectName(QStringLiteral("skippedSummary"));
    layout->addWidget(summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Path"), tr("Reason")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    const int shown = static_cast<int>(
        std::min<std::size_t>(skipped.size(), static_cast<std::size_t>(kMaxShownRows)));
    m_table->setRowCount(shown);
    for (int row = 0; row < shown; ++row) {
        const auto& skip = skipped[static_cast<std::size_t>(row)];
        const QString path = QString::fromStdString(skip.path.string());
        auto* pathItem = new QTableWidgetItem(path);
        pathItem->setToolTip(path);
        m_table->setItem(row, 0, pathItem);
        m_table->setItem(row, 1, new QTableWidgetItem(reasonText(skip)));
    }
    layout->addWidget(m_table, 1);

    auto* note = new QLabel(this);
    note->setObjectName(QStringLiteral("skippedNote"));
    QStringList notes;
    if (static_cast<std::uintmax_t>(skipped.size()) < totalSkipped) {
        notes << tr("%1 of %2 skipped entries are shown.")
                     .arg(skipped.size())
                     .arg(totalSkipped);
    }
    if (skipped.size() > static_cast<std::size_t>(shown)) {
        notes << tr("The table shows the first %1 entries.").arg(shown);
    }
    note->setText(notes.join(QStringLiteral(" ")));
    note->setVisible(!notes.isEmpty());
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::clicked, this, [this]() { accept(); });
    layout->addWidget(buttons);
}

int SkippedPathsDialog::rowCount() const {
    return m_table ? m_table->rowCount() : 0;
}

QString SkippedPathsDialog::pathAt(int row) const {
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return {};
    }
    const QTableWidgetItem* item = m_table->item(row, 0);
    return item ? item->text() : QString();
}

QString SkippedPathsDialog::reasonAt(int row) const {
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return {};
    }
    const QTableWidgetItem* item = m_table->item(row, 1);
    return item ? item->text() : QString();
}

} // namespace driverse::gui
