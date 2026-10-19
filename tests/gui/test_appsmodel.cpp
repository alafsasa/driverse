// Deterministic tests for the Phase 6 app-storage table model and its sort/
// filter proxy (the dialog itself is exercised manually — see
// docs/MANUAL_TEST_CHECKLIST.md).
#include "gui/appsdialog.h"
#include "gui/formatting.h"
#include "platform/packages.h"

#include <QCoreApplication>
#include <QModelIndex>
#include <QVariant>

#include <cassert>
#include <iostream>
#include <vector>

using driverse::gui::PackagesModel;
using driverse::gui::PackageSortFilterProxy;
using driverse::platform::PackageInfo;

namespace {

std::vector<PackageInfo> samplePackages() {
    return {
        {"firefox", "128.0", 300ull * 1024 * 1024, "dpkg"},
        {"htop", "3.3.0", 2ull * 1024 * 1024, "pacman"},
        {"org.gnome.Calculator", "45.2", 900ull * 1024 * 1024, "flatpak"},
        {"linux", "6.12.1", 150ull * 1024 * 1024, "pacman"},
    };
}

void testModel() {
    PackagesModel model;
    model.setPackages(samplePackages());
    assert(model.rowCount() == 4);
    assert(model.columnCount() == 4);

    // The size column carries raw bytes on UserRole (for numeric sorting) and a
    // human-readable string on DisplayRole.
    const QModelIndex size0 = model.index(0, PackagesModel::ColSize);
    assert(size0.data(Qt::UserRole).toLongLong() ==
           static_cast<qint64>(300ull * 1024 * 1024));
    assert(size0.data(Qt::DisplayRole).toString() ==
           driverse::gui::formatting::bytes(300ull * 1024 * 1024));
    assert(model.index(0, PackagesModel::ColName).data().toString() == "firefox");
    assert(model.index(0, PackagesModel::ColVersion).data().toString() == "128.0");
    assert(model.index(0, PackagesModel::ColSource).data().toString() == "dpkg");

    // packageAt round-trips and rejects invalid indexes.
    assert(model.packageAt(model.index(2, 0))->name == "org.gnome.Calculator");
    assert(model.packageAt(QModelIndex()) == nullptr);
    assert(model.packageAt(model.index(99, 0)) == nullptr);

    // Zero sizes render as a placeholder rather than "0 B".
    PackagesModel zero;
    zero.setPackages({{"nodata", "1.0", 0, "snap"}});
    assert(zero.index(0, PackagesModel::ColSize).data().toString() == "—");
    std::cout << "  PackagesModel OK\n";
}

void testNumericSort() {
    PackagesModel model;
    model.setPackages(samplePackages());
    PackageSortFilterProxy proxy;
    proxy.setSourceModel(&model);

    // Largest first: flatpak 900M, dpkg 300M, linux 150M, htop 2M.
    proxy.sort(PackagesModel::kSizeColumn, Qt::DescendingOrder);
    assert(proxy.index(0, PackagesModel::ColName).data().toString() ==
           "org.gnome.Calculator");
    assert(proxy.index(1, PackagesModel::ColName).data().toString() == "firefox");
    assert(proxy.index(2, PackagesModel::ColName).data().toString() == "linux");
    assert(proxy.index(3, PackagesModel::ColName).data().toString() == "htop");

    proxy.sort(PackagesModel::kSizeColumn, Qt::AscendingOrder);
    assert(proxy.index(0, PackagesModel::ColName).data().toString() == "htop");
    assert(proxy.index(3, PackagesModel::ColName).data().toString() ==
           "org.gnome.Calculator");
    std::cout << "  numeric size sort OK\n";
}

void testFilter() {
    PackagesModel model;
    model.setPackages(samplePackages());
    PackageSortFilterProxy proxy;
    proxy.setSourceModel(&model);

    assert(proxy.rowCount() == 4);
    proxy.setFilterText("pacman"); // matches by source
    assert(proxy.rowCount() == 2);
    proxy.setFilterText("fire"); // matches by name substring
    assert(proxy.rowCount() == 1);
    assert(proxy.index(0, PackagesModel::ColName).data().toString() == "firefox");
    proxy.setFilterText("FIREFOX"); // case-insensitive
    assert(proxy.rowCount() == 1);
    proxy.setFilterText("   "); // whitespace-only is treated as no filter
    assert(proxy.rowCount() == 4);
    proxy.setFilterText("nothing-matches-this");
    assert(proxy.rowCount() == 0);
    proxy.setFilterText(QString());
    assert(proxy.rowCount() == 4);
    std::cout << "  filter OK\n";
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << "Running app-storage model tests...\n";
    testModel();
    testNumericSort();
    testFilter();
    std::cout << "All app-storage model tests passed!\n";
    return 0;
}
