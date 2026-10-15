#pragma once

#include "core/types.h"

#include <QAbstractTableModel>

#include <vector>

namespace driverse::gui {

// Table model exposing discovered filesystems (mounts) for the dashboard.
class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        MountPoint = 0,
        Device,
        FsType,
        Total,
        Used,
        Free,
        Usage,
        ColumnCount
    };

    explicit DeviceTableModel(QObject* parent = nullptr);

    void setDevices(std::vector<core::DeviceInfo> devices);

    // Returns nullptr if row is out of range.
    const core::DeviceInfo* deviceAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::DeviceInfo> m_devices;
};

} // namespace driverse::gui
