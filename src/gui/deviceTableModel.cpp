#include "deviceTableModel.h"

#include "formatting.h"

#include <QBrush>
#include <QColor>

namespace driverse::gui {

namespace {

// Usage fraction in [0.0, 1.0]; 0 when capacity is unknown.
double usageFraction(const core::DeviceInfo& d) {
    if (d.totalBytes == 0) {
        return 0.0;
    }
    double f = static_cast<double>(d.usedBytes) / static_cast<double>(d.totalBytes);
    if (f < 0.0) return 0.0;
    if (f > 1.0) return 1.0;
    return f;
}

} // namespace

DeviceTableModel::DeviceTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void DeviceTableModel::setDevices(std::vector<core::DeviceInfo> devices) {
    beginResetModel();
    m_devices = std::move(devices);
    endResetModel();
}

const core::DeviceInfo* DeviceTableModel::deviceAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_devices.size())) {
        return nullptr;
    }
    return &m_devices[static_cast<std::size_t>(row)];
}

int DeviceTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_devices.size());
}

int DeviceTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_devices.size())) {
        return {};
    }
    const core::DeviceInfo& d = m_devices[static_cast<std::size_t>(index.row())];
    const bool special = d.isPseudoFs || d.isNetworkFs;

    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case MountPoint: return QString::fromStdString(d.mountPoint);
                case Device:     return QString::fromStdString(d.device);
                case FsType: {
                    QString type = QString::fromStdString(d.fsType);
                    if (d.isNetworkFs) type += QStringLiteral(" (remote)");
                    else if (d.isPseudoFs) type += QStringLiteral(" (virtual)");
                    else if (d.isRemovable) type += QStringLiteral(" (removable)");
                    return type;
                }
                case Total: return formatting::bytes(d.totalBytes);
                case Used:  return formatting::bytes(d.usedBytes);
                case Free:  return formatting::bytes(d.availableBytes);
                case Usage: return QString::number(usageFraction(d) * 100.0, 'f', 0) + "%";
                default:    return {};
            }

        case Qt::TextAlignmentRole:
            switch (index.column()) {
                case Total:
                case Used:
                case Free:
                case Usage:
                    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
                default:
                    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
            }

        case Qt::ForegroundRole:
            if (special) {
                return QBrush(QColor(140, 140, 140));
            }
            if (index.column() == Usage && usageFraction(d) >= 0.9) {
                return QBrush(QColor(200, 60, 60));
            }
            return {};

        case Qt::ToolTipRole: {
            QString text = QStringLiteral("device: %1\nfs: %2\noptions: %3")
                .arg(QString::fromStdString(d.device),
                     QString::fromStdString(d.fsType),
                     QString::fromStdString(d.options));
            if (!d.parentDisk.empty()) {
                text += QStringLiteral("\ndisk: %1%2%3")
                    .arg(QString::fromStdString(d.parentDisk),
                         d.isPartition ? QStringLiteral(" (partition)")
                                       : QStringLiteral(" (whole disk)"),
                         d.isRemovable ? QStringLiteral(", removable") : QString());
            }
            return text;
        }

        default:
            return {};
    }
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
        case MountPoint: return QStringLiteral("Mount point");
        case Device:     return QStringLiteral("Device");
        case FsType:     return QStringLiteral("Type");
        case Total:      return QStringLiteral("Total");
        case Used:       return QStringLiteral("Used");
        case Free:       return QStringLiteral("Free");
        case Usage:      return QStringLiteral("Usage");
        default:         return {};
    }
}

} // namespace driverse::gui
