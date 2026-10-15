#include "core/types.h"
#include "gui/deviceTableModel.h"

#include <QCoreApplication>
#include <QModelIndex>
#include <QVariant>

#include <cassert>
#include <iostream>

using driverse::core::DeviceInfo;
using driverse::gui::DeviceTableModel;

namespace {

DeviceInfo makeExt4() {
    DeviceInfo d;
    d.device = "/dev/sda1";
    d.mountPoint = "/";
    d.fsType = "ext4";
    d.totalBytes = 1000;
    d.usedBytes = 250;
    d.freeBytes = 750;
    d.availableBytes = 750;
    return d;
}

DeviceInfo makeNfs() {
    DeviceInfo d;
    d.device = "server:/export";
    d.mountPoint = "/mnt/net";
    d.fsType = "nfs";
    d.totalBytes = 2000;
    d.usedBytes = 100;
    d.availableBytes = 1900;
    d.isNetworkFs = true;
    return d;
}

DeviceInfo makeProc() {
    DeviceInfo d;
    d.device = "proc";
    d.mountPoint = "/proc";
    d.fsType = "proc";
    d.totalBytes = 0;
    d.usedBytes = 0;
    d.availableBytes = 0;
    d.isPseudoFs = true;
    return d;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << "Running device table model tests...\n";

    DeviceTableModel model;
    assert(model.rowCount() == 0);
    assert(model.columnCount() == 7);
    assert(model.deviceAt(0) == nullptr);

    std::vector<DeviceInfo> devices{makeExt4(), makeNfs(), makeProc()};
    model.setDevices(devices);

    assert(model.rowCount() == 3);
    assert(model.columnCount() == DeviceTableModel::ColumnCount);

    // Bounds checking.
    assert(model.deviceAt(0) != nullptr);
    assert(model.deviceAt(2) != nullptr);
    assert(model.deviceAt(-1) == nullptr);
    assert(model.deviceAt(3) == nullptr);

    auto index = [&](int row, int col) { return model.index(row, col); };

    // Display values.
    assert(model.data(index(0, DeviceTableModel::MountPoint), Qt::DisplayRole).toString() == "/");
    assert(model.data(index(0, DeviceTableModel::Usage), Qt::DisplayRole).toString() == "25%");
    // Unknown capacity -> 0%, not a crash / div-by-zero.
    assert(model.data(index(2, DeviceTableModel::Usage), Qt::DisplayRole).toString() == "0%");

    // Special filesystem markers.
    assert(model.data(index(1, DeviceTableModel::FsType), Qt::DisplayRole)
               .toString()
               .contains(QStringLiteral("(remote)")));
    assert(model.data(index(2, DeviceTableModel::FsType), Qt::DisplayRole)
               .toString()
               .contains(QStringLiteral("(virtual)")));

    // Headers and alignment.
    assert(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString() == "Mount point");
    const int usedAlign =
        model.data(index(0, DeviceTableModel::Used), Qt::TextAlignmentRole).toInt();
    assert((usedAlign & static_cast<int>(Qt::AlignRight)) != 0);

    // Foreground: grey for virtual/remote, none for ordinary rows.
    assert(model.data(index(1, DeviceTableModel::MountPoint), Qt::ForegroundRole).isValid());
    assert(!model.data(index(0, DeviceTableModel::MountPoint), Qt::ForegroundRole).isValid());

    // High usage gets highlighted.
    DeviceInfo hot;
    hot.mountPoint = "/data";
    hot.device = "/dev/sdb1";
    hot.fsType = "xfs";
    hot.totalBytes = 100;
    hot.usedBytes = 95;
    hot.availableBytes = 5;
    model.setDevices({hot});
    assert(model.rowCount() == 1);
    assert(model.data(model.index(0, DeviceTableModel::Usage), Qt::DisplayRole).toString() == "95%");
    assert(model.data(model.index(0, DeviceTableModel::Usage), Qt::ForegroundRole).isValid());

    std::cout << "All device table model tests passed!\n";
    return 0;
}
