#pragma once

#include "core/types.h"
#include "scandata.h"

#include <QAbstractTableModel>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

#include <cstdint>
#include <optional>
#include <vector>

namespace driverse::gui {

// Filterable, sortable, virtual table model over a ScanData snapshot.
//
// One model instance serves one "mode" (which source vector it exposes). Rows
// are addressed through a visible-index list, so filtering/sorting never copies
// the underlying items — the view can handle very large result sets.
class ResultsTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum class Mode {
        Files,           // ScanResult::largestFiles
        Directories,     // ScanResult::directories
        Recommendations  // ScanData::recommendations (checkable)
    };

    // All modes expose exactly 7 columns; their meaning varies by mode:
    //   Files:          Name, Path, Type, Logical, Allocated, Modified, Risk
    //   Directories:    Name, Path, Files, Dirs, Allocated, Modified, Risk
    //   Recommendations:Name, Path, Category, Risk, Recoverable, Confidence, Justification
    static constexpr int kColumnCount = 7;

    explicit ResultsTableModel(Mode mode, QObject* parent = nullptr);

    Mode mode() const { return m_mode; }

    void setData(const ScanDataPtr& data);
    ScanDataPtr data() const { return m_data; }

    // ---- Filtering ----
    // riskMask: bit (1 << static_cast<int>(RiskLevel)) enabled. Rows without a
    // known risk (non-recommended files/dirs) always pass the risk filter.
    void setFilters(const QString& pathSubstring, std::uintmax_t minBytes,
                    int riskMask, bool recommendedOnly);
    void clearFilters();

    // ---- Qt model interface ----
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // ---- Row helpers (valid for a *visible* row, not a source index) ----
    QString pathAt(int row) const;
    QString detailsHtml(int row) const;
    QString detailsText(int row) const;

    // ---- Recommendations selection ----
    int checkedCount() const;
    std::uintmax_t checkedRecoverable() const;
    std::vector<core::CleanupCandidate> checkedCandidates() const;
    // Select/deselect all *visible* rows; riskFilter < 0 means every risk.
    void setVisibleChecked(bool checked, int riskFilter = -1);

signals:
    void checkedChanged();

private:
    int sourceCount() const;
    bool passesFilters(int src) const;
    std::filesystem::path pathForSource(int src) const;
    std::uintmax_t sizeForSource(int src) const;
    std::optional<core::RiskLevel> riskForSource(int src) const;
    QVariant displayText(int src, int column) const;
    QVariant sortKey(int src, int column) const;
    QVector<QPair<QString, QString>> detailFields(int src) const;
    QVariant alignmentFor(int column) const;
    void rebuild();

    Mode m_mode;
    ScanDataPtr m_data;
    std::vector<int> m_visible;   // visible row -> source index
    std::vector<bool> m_checked;  // per source index (Recommendations only)

    // Filters
    QString m_pathFilter;
    std::uintmax_t m_minBytes = 0;
    int m_riskMask = 0x1F; // all five levels enabled
    bool m_recommendedOnly = false;

    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

} // namespace driverse::gui
