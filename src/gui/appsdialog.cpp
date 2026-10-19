#include "appsdialog.h"

#include "formatting.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QtConcurrent>

#include <cstdint>

using driverse::platform::PackageInfo;
using driverse::platform::PackageInventory;

namespace driverse::gui {

// --- PackagesModel -----------------------------------------------------------

PackagesModel::PackagesModel(QObject* parent) : QAbstractTableModel(parent) {}

void PackagesModel::setPackages(std::vector<PackageInfo> packages) {
    beginResetModel();
    m_packages = std::move(packages);
    endResetModel();
}

const PackageInfo* PackagesModel::packageAt(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_packages.size())) {
        return nullptr;
    }
    return &m_packages[static_cast<std::size_t>(index.row())];
}

int PackagesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_packages.size());
}

int PackagesModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PackagesModel::data(const QModelIndex& index, int role) const {
    const PackageInfo* p = packageAt(index);
    if (!p) return {};
    if (index.column() == ColSize) {
        if (role == Qt::UserRole) return static_cast<qint64>(p->sizeBytes);
        if (role == Qt::DisplayRole) {
            return p->sizeBytes > 0 ? formatting::bytes(p->sizeBytes)
                                    : QStringLiteral("—");
        }
        return {};
    }
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
        case ColName: return QString::fromStdString(p->name);
        case ColVersion: return QString::fromStdString(p->version);
        case ColSource: return QString::fromStdString(p->source);
        default: return {};
    }
}

QVariant PackagesModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case ColName: return tr("Application");
        case ColVersion: return tr("Version");
        case ColSize: return tr("Installed size");
        case ColSource: return tr("Source");
        default: return {};
    }
}

// --- PackageSortFilterProxy --------------------------------------------------

PackageSortFilterProxy::PackageSortFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void PackageSortFilterProxy::setFilterText(const QString& text) {
    m_filter = text.trimmed().toLower();
#if QT_VERSION >= QT_VERSION_CHECK(6, 13, 0)
    beginFilterChange(Direction::Rows);
    endFilterChange(Direction::Rows);
#else
    beginFilterChange();
    endFilterChange(Direction::Rows);
#endif
}

bool PackageSortFilterProxy::filterAcceptsRow(int row,
                                              const QModelIndex& parent) const {
    if (m_filter.isEmpty()) return true;
    const QString name =
        sourceModel()->index(row, PackagesModel::ColName, parent).data().toString();
    const QString source =
        sourceModel()->index(row, PackagesModel::ColSource, parent).data().toString();
    return name.toLower().contains(m_filter) || source.toLower().contains(m_filter);
}

bool PackageSortFilterProxy::lessThan(const QModelIndex& left,
                                      const QModelIndex& right) const {
    if (left.column() == PackagesModel::kSizeColumn) {
        const qint64 l = sourceModel()->data(left, Qt::UserRole).toLongLong();
        const qint64 r = sourceModel()->data(right, Qt::UserRole).toLongLong();
        return l < r;
    }
    return QSortFilterProxyModel::lessThan(left, right);
}

// --- AppsDialog ---------------------------------------------------------------

AppsDialog::AppsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Application Storage"));
    resize(860, 580);

    auto* root = new QVBoxLayout(this);

    auto* banner = new QLabel(
        tr("Read-only inventory of installed applications, queried from this "
           "system's package managers. Driverse never installs, removes or "
           "modifies packages — the removal command below is shown for "
           "copy/paste into a terminal.\n"
           "Applications share dependencies: removing a package frees less "
           "than its listed size and can break other applications. Review "
           "with your package manager before removing anything."),
        this);
    banner->setWordWrap(true);
    root->addWidget(banner);

    auto* filterRow = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter by application or source..."));
    filterRow->addWidget(m_filter, 1);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    filterRow->addWidget(m_refreshButton);
    root->addLayout(filterRow);

    m_model = new PackagesModel(this);
    m_proxy = new PackageSortFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_table = new QTableView(this);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(PackagesModel::kSizeColumn, Qt::DescendingOrder);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setTextFormat(Qt::RichText);
    root->addWidget(m_details);

    auto* actionRow = new QHBoxLayout;
    m_totals = new QLabel(this);
    actionRow->addWidget(m_totals, 1);
    m_copyButton = new QPushButton(tr("Copy removal command"), this);
    m_copyButton->setEnabled(false);
    actionRow->addWidget(m_copyButton);
    auto* closeButton = new QPushButton(tr("Close"), this);
    actionRow->addWidget(closeButton);
    root->addLayout(actionRow);

    m_status = new QLabel(this);
    root->addWidget(m_status);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_refreshButton, &QPushButton::clicked, this, &AppsDialog::refresh);
    connect(m_filter, &QLineEdit::textChanged, this, &AppsDialog::onFilterChanged);
    connect(m_copyButton, &QPushButton::clicked, this, &AppsDialog::copyCommand);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &AppsDialog::onSelectionChanged);

    m_watcher = new QFutureWatcher<std::vector<PackageInfo>>(this);
    connect(m_watcher, &QFutureWatcher<std::vector<PackageInfo>>::finished, this,
            [this]() {
                m_loading = false;
                m_refreshButton->setEnabled(true);
                populate(m_watcher->result());
            });

    refresh();
}

void AppsDialog::refresh() {
    if (m_loading) return;
    m_loading = true;
    m_refreshButton->setEnabled(false);
    m_status->setText(tr("Querying package managers (read-only)..."));
    m_watcher->setFuture(
        QtConcurrent::run([]() { return PackageInventory().inventory(); }));
}

void AppsDialog::populate(const std::vector<PackageInfo>& packages) {
    m_model->setPackages(packages);
    m_proxy->sort(m_proxy->sortColumn(), m_proxy->sortOrder());

    std::uint64_t total = 0;
    for (const auto& p : packages) total += p.sizeBytes;
    m_totals->setText(
        tr("%1 applications — %2 total installed size (approximate, shared "
           "dependencies included)")
            .arg(packages.size())
            .arg(formatting::bytes(total)));

    QStringList backends;
    for (const auto& b : PackageInventory::availableBackends()) {
        backends << QString::fromStdString(b);
    }
    m_status->setText(backends.isEmpty()
                          ? tr("No package managers found on this system.")
                          : tr("Queried: %1").arg(backends.join(QStringLiteral(", "))));

    m_details->clear();
    m_copyButton->setEnabled(false);
}

void AppsDialog::onFilterChanged(const QString& text) {
    m_proxy->setFilterText(text);
}

void AppsDialog::onSelectionChanged() {
    const QModelIndex src = m_proxy->mapToSource(m_table->currentIndex());
    const PackageInfo* p = m_model->packageAt(src);
    if (!p) {
        m_details->clear();
        m_copyButton->setEnabled(false);
        return;
    }
    const std::string hint = driverse::platform::removalHint(*p);
    m_details->setText(
        tr("<b>%1</b> %2 — %3<br>Installed size: %4 (approximate; dependencies "
           "are shared with other applications, so actual freed space will "
           "differ).<br>To remove, run in a terminal:<br><code>%5</code>")
            .arg(QString::fromStdString(p->name), QString::fromStdString(p->version),
                 QString::fromStdString(p->source),
                 p->sizeBytes > 0 ? formatting::bytes(p->sizeBytes)
                                  : QString::fromLatin1("?"),
                 QString::fromStdString(hint.empty() ? tr("(no known command)").toStdString()
                                                     : hint)));
    m_copyButton->setEnabled(!hint.empty());
}

void AppsDialog::copyCommand() {
    const QModelIndex src = m_proxy->mapToSource(m_table->currentIndex());
    const PackageInfo* p = m_model->packageAt(src);
    if (!p) return;
    const std::string hint = driverse::platform::removalHint(*p);
    if (hint.empty()) return;
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(QString::fromStdString(hint));
        m_status->setText(tr("Copied: %1").arg(QString::fromStdString(hint)));
    }
}

} // namespace driverse::gui
