#pragma once

#include "platform/packages.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QAbstractTableModel>

#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace driverse::gui {

// Table model over the read-only package inventory: Name, Version, Size,
// Source. The size column exposes the raw byte count via Qt::UserRole so the
// proxy can sort numerically instead of lexically.
class PackagesModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Columns { ColName = 0, ColVersion, ColSize, ColSource, ColumnCount };
    static constexpr int kSizeColumn = ColSize;

    explicit PackagesModel(QObject* parent = nullptr);

    void setPackages(std::vector<platform::PackageInfo> packages);
    const platform::PackageInfo* packageAt(const QModelIndex& index) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    std::vector<platform::PackageInfo> m_packages;
};

// Text filter over name/source plus numeric sorting for the size column.
class PackageSortFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PackageSortFilterProxy(QObject* parent = nullptr);

    void setFilterText(const QString& text);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString m_filter;
};

// "Application Storage" dialog (Phase 6): a query-only inventory of installed
// applications. Deliberately has *no* remove/install actions — it shows the
// package managers' own removal command as copyable text and states the
// shared-dependency caveat.
class AppsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppsDialog(QWidget* parent = nullptr);

private slots:
    void refresh();
    void onFilterChanged(const QString& text);
    void onSelectionChanged();
    void copyCommand();

private:
    void populate(const std::vector<platform::PackageInfo>& packages);

    PackagesModel* m_model = nullptr;
    PackageSortFilterProxy* m_proxy = nullptr;
    QTableView* m_table = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_totals = nullptr;
    QLabel* m_details = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QFutureWatcher<std::vector<platform::PackageInfo>>* m_watcher = nullptr;
    bool m_loading = false;
};

} // namespace driverse::gui
