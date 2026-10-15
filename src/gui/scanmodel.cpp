#include "scanmodel.h"

#include "core/recommendations.h"
#include "core/utils.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QPair>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstdint>

namespace driverse::gui {

namespace {

// Upper bound on exposed rows so a pathological scan cannot stall the view.
// The default sort is by size, so the cap keeps the largest items.
constexpr int kMaxRows = 200000;

QString bytes(std::uintmax_t value) {
    return QString::fromStdString(core::utils::formatBytes(value));
}

QString timeToString(const std::chrono::system_clock::time_point& tp) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    if (secs.count() <= 0) {
        return QStringLiteral("\u2014");
    }
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs.count()));
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

qulonglong timeKey(const std::chrono::system_clock::time_point& tp) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    return secs.count() > 0 ? static_cast<qulonglong>(secs.count()) : 0ULL;
}

QString fileTypeToString(core::FileType type) {
    switch (type) {
        case core::FileType::RegularFile: return QStringLiteral("File");
        case core::FileType::Directory:   return QStringLiteral("Dir");
        case core::FileType::Symlink:     return QStringLiteral("Symlink");
        case core::FileType::Other:       return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

QString riskToString(core::RiskLevel risk) {
    return QString::fromStdString(core::utils::riskLevelToString(risk));
}

QString confidenceToString(core::ConfidenceLevel level) {
    return QString::fromStdString(core::utils::confidenceLevelToString(level));
}

QColor riskColor(core::RiskLevel risk) {
    switch (risk) {
        case core::RiskLevel::Safe:            return QColor(45, 143, 78);
        case core::RiskLevel::Low:             return QColor(45, 128, 160);
        case core::RiskLevel::Medium:          return QColor(191, 138, 32);
        case core::RiskLevel::High:            return QColor(200, 70, 60);
        case core::RiskLevel::SystemProtected: return QColor(120, 60, 150);
    }
    return QColor(90, 90, 96);
}

QString fileNameOf(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    if (name.empty()) {
        name = path.string();
    }
    return QString::fromStdString(name);
}

} // namespace

ResultsTableModel::ResultsTableModel(Mode mode, QObject* parent)
    : QAbstractTableModel(parent), m_mode(mode) {
    switch (m_mode) {
        case Mode::Files:
        case Mode::Directories:
            m_sortColumn = 4; // Allocated size
            m_sortOrder = Qt::DescendingOrder;
            break;
        case Mode::Recommendations:
            m_sortColumn = 3; // Risk
            m_sortOrder = Qt::AscendingOrder;
            break;
    }
}

int ResultsTableModel::sourceCount() const {
    if (!m_data) {
        return 0;
    }
    switch (m_mode) {
        case Mode::Files:           return static_cast<int>(m_data->result.largestFiles.size());
        case Mode::Directories:     return static_cast<int>(m_data->result.directories.size());
        case Mode::Recommendations: return static_cast<int>(m_data->recommendations.size());
    }
    return 0;
}

std::filesystem::path ResultsTableModel::pathForSource(int src) const {
    switch (m_mode) {
        case Mode::Files:           return m_data->result.largestFiles[static_cast<std::size_t>(src)].path;
        case Mode::Directories:     return m_data->result.directories[static_cast<std::size_t>(src)].path;
        case Mode::Recommendations: return m_data->recommendations[static_cast<std::size_t>(src)].path;
    }
    return {};
}

std::uintmax_t ResultsTableModel::sizeForSource(int src) const {
    switch (m_mode) {
        case Mode::Files:
            return m_data->result.largestFiles[static_cast<std::size_t>(src)].allocatedSize;
        case Mode::Directories:
            return m_data->result.directories[static_cast<std::size_t>(src)].uniqueAllocatedSize;
        case Mode::Recommendations:
            return m_data->recommendations[static_cast<std::size_t>(src)].allocatedSize;
    }
    return 0;
}

std::optional<core::RiskLevel> ResultsTableModel::riskForSource(int src) const {
    if (!m_data) {
        return std::nullopt;
    }
    if (m_mode == Mode::Recommendations) {
        return m_data->recommendations[static_cast<std::size_t>(src)].risk;
    }
    const auto it = m_data->riskByPath.find(pathForSource(src).string());
    if (it == m_data->riskByPath.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool ResultsTableModel::passesFilters(int src) const {
    const std::optional<core::RiskLevel> risk = riskForSource(src);

    if (m_mode != Mode::Recommendations && m_recommendedOnly && !risk) {
        return false;
    }
    if (risk && (m_riskMask & (1 << static_cast<int>(*risk))) == 0) {
        return false;
    }
    if (!m_pathFilter.isEmpty()) {
        const QString path = QString::fromStdString(pathForSource(src).string());
        if (!path.contains(m_pathFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (m_minBytes > 0 && sizeForSource(src) < m_minBytes) {
        return false;
    }
    return true;
}

void ResultsTableModel::rebuild() {
    beginResetModel();
    m_visible.clear();

    if (m_data) {
        const int n = sourceCount();
        for (int i = 0; i < n; ++i) {
            if (passesFilters(i)) {
                m_visible.push_back(i);
            }
        }

        const int column = m_sortColumn;
        const Qt::SortOrder order = m_sortOrder;
        std::stable_sort(m_visible.begin(), m_visible.end(),
                         [this, column, order](int a, int b) {
                             const QVariant ka = sortKey(a, column);
                             const QVariant kb = sortKey(b, column);
                             const bool numeric =
                                 ka.typeId() == QMetaType::ULongLong ||
                                 ka.typeId() == QMetaType::LongLong ||
                                 ka.typeId() == QMetaType::Int;
                             int cmp = 0;
                             if (numeric) {
                                 const qulonglong va = ka.toULongLong();
                                 const qulonglong vb = kb.toULongLong();
                                 cmp = (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                             } else {
                                 cmp = QString::compare(ka.toString(), kb.toString(),
                                                        Qt::CaseInsensitive);
                                 cmp = (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
                             }
                             if (cmp == 0) {
                                 return a < b; // stable tie-break by source order
                             }
                             return order == Qt::AscendingOrder ? (cmp < 0) : (cmp > 0);
                         });

        if (static_cast<int>(m_visible.size()) > kMaxRows) {
            m_visible.resize(static_cast<std::size_t>(kMaxRows));
        }
    }

    endResetModel();
}

void ResultsTableModel::setData(const ScanDataPtr& data) {
    m_data = data;
    m_checked.assign(static_cast<std::size_t>(sourceCount()), false);
    rebuild();
}

void ResultsTableModel::setFilters(const QString& pathSubstring, std::uintmax_t minBytes,
                                   int riskMask, bool recommendedOnly) {
    m_pathFilter = pathSubstring;
    m_minBytes = minBytes;
    m_riskMask = riskMask & 0x1F;
    m_recommendedOnly = recommendedOnly;
    rebuild();
}

void ResultsTableModel::clearFilters() {
    m_pathFilter.clear();
    m_minBytes = 0;
    m_riskMask = 0x1F;
    m_recommendedOnly = false;
    rebuild();
}

int ResultsTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_visible.size());
}

int ResultsTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return kColumnCount;
}

QVariant ResultsTableModel::displayText(int src, int column) const {
    switch (m_mode) {
        case Mode::Files: {
            const auto& f = m_data->result.largestFiles[static_cast<std::size_t>(src)];
            switch (column) {
                case 0: return fileNameOf(f.path);
                case 1: return QString::fromStdString(f.path.string());
                case 2: return fileTypeToString(f.type);
                case 3: return bytes(f.logicalSize);
                case 4: return bytes(f.allocatedSize);
                case 5: return timeToString(f.mtime);
                case 6: {
                    const auto r = riskForSource(src);
                    return r ? riskToString(*r) : QStringLiteral("\u2014");
                }
                default: return {};
            }
        }
        case Mode::Directories: {
            const auto& d = m_data->result.directories[static_cast<std::size_t>(src)];
            switch (column) {
                case 0: return fileNameOf(d.path);
                case 1: return QString::fromStdString(d.path.string());
                case 2: return QString::number(d.fileCount);
                case 3: return QString::number(d.dirCount);
                case 4: return bytes(d.uniqueAllocatedSize);
                case 5: return timeToString(d.mtime);
                case 6: {
                    const auto r = riskForSource(src);
                    return r ? riskToString(*r) : QStringLiteral("\u2014");
                }
                default: return {};
            }
        }
        case Mode::Recommendations: {
            const auto& c = m_data->recommendations[static_cast<std::size_t>(src)];
            switch (column) {
                case 0: return fileNameOf(c.path);
                case 1: return QString::fromStdString(c.path.string());
                case 2: return QString::fromStdString(core::categoryToString(c.category));
                case 3: return riskToString(c.risk);
                case 4: return bytes(c.allocatedSize);
                case 5: return confidenceToString(c.confidence);
                case 6: return QString::fromStdString(c.justification);
                default: return {};
            }
        }
    }
    return {};
}

QVariant ResultsTableModel::sortKey(int src, int column) const {
    switch (m_mode) {
        case Mode::Files: {
            const auto& f = m_data->result.largestFiles[static_cast<std::size_t>(src)];
            if (column == 3) return QVariant::fromValue<qulonglong>(f.logicalSize);
            if (column == 4) return QVariant::fromValue<qulonglong>(f.allocatedSize);
            if (column == 5) return QVariant::fromValue<qulonglong>(timeKey(f.mtime));
            if (column == 6) {
                const auto r = riskForSource(src);
                return QVariant(static_cast<int>(r ? static_cast<int>(*r) : 99));
            }
            break;
        }
        case Mode::Directories: {
            const auto& d = m_data->result.directories[static_cast<std::size_t>(src)];
            if (column == 2) return QVariant::fromValue<qulonglong>(d.fileCount);
            if (column == 3) return QVariant::fromValue<qulonglong>(d.dirCount);
            if (column == 4) return QVariant::fromValue<qulonglong>(d.uniqueAllocatedSize);
            if (column == 5) return QVariant::fromValue<qulonglong>(timeKey(d.mtime));
            if (column == 6) {
                const auto r = riskForSource(src);
                return QVariant(static_cast<int>(r ? static_cast<int>(*r) : 99));
            }
            break;
        }
        case Mode::Recommendations: {
            const auto& c = m_data->recommendations[static_cast<std::size_t>(src)];
            if (column == 3) return QVariant(static_cast<int>(c.risk));
            if (column == 4) return QVariant::fromValue<qulonglong>(c.allocatedSize);
            break;
        }
    }
    return displayText(src, column);
}

QVariant ResultsTableModel::alignmentFor(int column) const {
    bool numeric = false;
    switch (m_mode) {
        case Mode::Files:       numeric = (column == 3 || column == 4); break;
        case Mode::Directories: numeric = (column == 2 || column == 3 || column == 4); break;
        case Mode::Recommendations: numeric = (column == 4); break;
    }
    return static_cast<int>(numeric ? (Qt::AlignRight | Qt::AlignVCenter)
                                    : (Qt::AlignLeft | Qt::AlignVCenter));
}

QVariant ResultsTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || !m_data) {
        return {};
    }
    const int row = index.row();
    const int src = m_visible[static_cast<std::size_t>(row)];

    if (role == Qt::CheckStateRole) {
        if (m_mode == Mode::Recommendations && index.column() == 0) {
            return m_checked[static_cast<std::size_t>(src)] ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    }

    switch (role) {
        case Qt::DisplayRole:
            return displayText(src, index.column());

        case Qt::TextAlignmentRole:
            return alignmentFor(index.column());

        case Qt::ForegroundRole: {
            const int riskColumn = (m_mode == Mode::Recommendations) ? 3 : 6;
            if (index.column() == riskColumn) {
                const auto r = riskForSource(src);
                if (r) {
                    return QBrush(riskColor(*r));
                }
            }
            return {};
        }

        case Qt::ToolTipRole: {
            const auto r = riskForSource(src);
            QString tip = QString::fromStdString(pathForSource(src).string());
            if (r) {
                tip += QStringLiteral("\nRisk: ") + riskToString(*r);
            }
            return tip;
        }

        default:
            return {};
    }
}

QVariant ResultsTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (m_mode) {
        case Mode::Files:
            switch (section) {
                case 0: return QStringLiteral("Name");
                case 1: return QStringLiteral("Path");
                case 2: return QStringLiteral("Type");
                case 3: return QStringLiteral("Logical");
                case 4: return QStringLiteral("Allocated");
                case 5: return QStringLiteral("Modified");
                case 6: return QStringLiteral("Risk");
            }
            break;
        case Mode::Directories:
            switch (section) {
                case 0: return QStringLiteral("Name");
                case 1: return QStringLiteral("Path");
                case 2: return QStringLiteral("Files");
                case 3: return QStringLiteral("Dirs");
                case 4: return QStringLiteral("Allocated");
                case 5: return QStringLiteral("Modified");
                case 6: return QStringLiteral("Risk");
            }
            break;
        case Mode::Recommendations:
            switch (section) {
                case 0: return QStringLiteral("Name");
                case 1: return QStringLiteral("Path");
                case 2: return QStringLiteral("Category");
                case 3: return QStringLiteral("Risk");
                case 4: return QStringLiteral("Recoverable");
                case 5: return QStringLiteral("Confidence");
                case 6: return QStringLiteral("Justification");
            }
            break;
    }
    return {};
}

Qt::ItemFlags ResultsTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (m_mode == Mode::Recommendations && index.column() == 0) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

bool ResultsTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::CheckStateRole || m_mode != Mode::Recommendations ||
        index.column() != 0 || !index.isValid() || index.row() < 0 ||
        index.row() >= rowCount()) {
        return false;
    }
    const int src = m_visible[static_cast<std::size_t>(index.row())];
    const bool checked = value.toInt() == Qt::Checked;
    if (m_checked[static_cast<std::size_t>(src)] == checked) {
        return true;
    }
    m_checked[static_cast<std::size_t>(src)] = checked;
    emit dataChanged(index, index, {Qt::CheckStateRole});
    emit checkedChanged();
    return true;
}

void ResultsTableModel::sort(int column, Qt::SortOrder order) {
    m_sortColumn = column;
    m_sortOrder = order;
    rebuild();
}

QString ResultsTableModel::pathAt(int row) const {
    if (row < 0 || row >= rowCount() || !m_data) {
        return {};
    }
    const int src = m_visible[static_cast<std::size_t>(row)];
    return QString::fromStdString(pathForSource(src).string());
}

QVector<QPair<QString, QString>> ResultsTableModel::detailFields(int src) const {
    QVector<QPair<QString, QString>> fields;
    fields.append({QStringLiteral("Path"),
                   QString::fromStdString(pathForSource(src).string())});

    switch (m_mode) {
        case Mode::Files: {
            const auto& f = m_data->result.largestFiles[static_cast<std::size_t>(src)];
            fields.append({QStringLiteral("Type"), fileTypeToString(f.type)});
            fields.append({QStringLiteral("Logical size"), bytes(f.logicalSize)});
            fields.append({QStringLiteral("Allocated size"), bytes(f.allocatedSize)});
            fields.append({QStringLiteral("Modified"), timeToString(f.mtime)});
            fields.append({QStringLiteral("Hard links"), QString::number(f.hardLinkCount)});
            fields.append({QStringLiteral("Inode"), QString::number(f.inode)});
            fields.append({QStringLiteral("Owner uid"), QString::number(f.ownerUid)});
            fields.append({QStringLiteral("Sparse"), f.isSparse ? QStringLiteral("yes")
                                                                : QStringLiteral("no")});
            fields.append({QStringLiteral("Confidence"), confidenceToString(f.sizeConfidence)});
            break;
        }
        case Mode::Directories: {
            const auto& d = m_data->result.directories[static_cast<std::size_t>(src)];
            fields.append({QStringLiteral("Logical size"), bytes(d.logicalSize)});
            fields.append({QStringLiteral("Allocated (unique)"), bytes(d.uniqueAllocatedSize)});
            fields.append({QStringLiteral("Files"), QString::number(d.fileCount)});
            fields.append({QStringLiteral("Subdirectories"), QString::number(d.dirCount)});
            fields.append({QStringLiteral("Depth"), QString::number(d.depth)});
            fields.append({QStringLiteral("Modified"), timeToString(d.mtime)});
            break;
        }
        case Mode::Recommendations: {
            const auto& c = m_data->recommendations[static_cast<std::size_t>(src)];
            fields.append({QStringLiteral("Category"),
                           QString::fromStdString(core::categoryToString(c.category))});
            fields.append({QStringLiteral("Risk"), riskToString(c.risk)});
            fields.append({QStringLiteral("Recoverable"), bytes(c.allocatedSize)});
            fields.append({QStringLiteral("Confidence"), confidenceToString(c.confidence)});
            fields.append({QStringLiteral("Modified"), timeToString(c.mtime)});
            fields.append({QStringLiteral("Justification"),
                           QString::fromStdString(c.justification)});
            if (!c.tags.empty()) {
                QStringList tags;
                for (const auto& tag : c.tags) {
                    tags << QString::fromStdString(tag);
                }
                fields.append({QStringLiteral("Tags"), tags.join(QStringLiteral(", "))});
            }
            break;
        }
    }

    const auto risk = riskForSource(src);
    if (risk && m_mode != Mode::Recommendations) {
        fields.append({QStringLiteral("Recommended risk"), riskToString(*risk)});
    }
    return fields;
}

QString ResultsTableModel::detailsText(int row) const {
    if (row < 0 || row >= rowCount() || !m_data) {
        return {};
    }
    const int src = m_visible[static_cast<std::size_t>(row)];
    QString out;
    for (const auto& field : detailFields(src)) {
        out += field.first + QStringLiteral(": ") + field.second + QLatin1Char('\n');
    }
    return out;
}

QString ResultsTableModel::detailsHtml(int row) const {
    if (row < 0 || row >= rowCount() || !m_data) {
        return QStringLiteral("<p style='color:#888'>No selection.</p>");
    }
    const int src = m_visible[static_cast<std::size_t>(row)];
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='3' width='100%'>");
    for (const auto& field : detailFields(src)) {
        html += QStringLiteral("<tr><td style='color:#86868e;white-space:nowrap;"
                               "vertical-align:top;'>%1</td>"
                               "<td style='color:#2b2b31;word-wrap:break-word;'>%2</td></tr>")
                    .arg(field.first.toHtmlEscaped(), field.second.toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
    return html;
}

int ResultsTableModel::checkedCount() const {
    return static_cast<int>(std::count(m_checked.begin(), m_checked.end(), true));
}

std::uintmax_t ResultsTableModel::checkedRecoverable() const {
    std::uintmax_t total = 0;
    if (m_mode != Mode::Recommendations || !m_data) {
        return 0;
    }
    for (std::size_t i = 0; i < m_checked.size(); ++i) {
        if (m_checked[i]) {
            total += m_data->recommendations[i].allocatedSize;
        }
    }
    return total;
}

std::vector<core::CleanupCandidate> ResultsTableModel::checkedCandidates() const {
    std::vector<core::CleanupCandidate> out;
    if (m_mode != Mode::Recommendations || !m_data) {
        return out;
    }
    for (std::size_t i = 0; i < m_checked.size(); ++i) {
        if (m_checked[i]) {
            out.push_back(m_data->recommendations[i]);
        }
    }
    return out;
}

void ResultsTableModel::setVisibleChecked(bool checked, int riskFilter) {
    if (m_mode != Mode::Recommendations) {
        return;
    }
    for (const int src : m_visible) {
        if (riskFilter >= 0) {
            const auto r = riskForSource(src);
            if (!r || static_cast<int>(*r) != riskFilter) {
                continue;
            }
        }
        m_checked[static_cast<std::size_t>(src)] = checked;
    }
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::CheckStateRole});
    }
    emit checkedChanged();
}

} // namespace driverse::gui
