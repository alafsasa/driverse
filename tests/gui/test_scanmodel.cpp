#include "core/types.h"
#include "gui/formatting.h"
#include "gui/scanmodel.h"

#include <QCoreApplication>
#include <QModelIndex>
#include <QVariant>

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using driverse::core::RiskLevel;
using driverse::gui::ResultsTableModel;
using driverse::gui::ScanData;
using driverse::gui::ScanDataPtr;

namespace {

driverse::core::FileItem makeFile(const std::string& path, std::uintmax_t allocated) {
    driverse::core::FileItem f{};
    f.path = path;
    f.name = f.path.filename();
    f.logicalSize = allocated;
    f.allocatedSize = allocated;
    f.mtime = std::chrono::system_clock::time_point{};
    f.type = driverse::core::FileType::RegularFile;
    f.inode = 1;
    f.deviceId = 1;
    f.ownerUid = 1000;
    f.isSymlink = false;
    f.isDirectory = false;
    f.isHardLinked = false;
    f.isProtected = false;
    f.isSparse = false;
    f.hardLinkCount = 1;
    f.sizeConfidence = driverse::core::ConfidenceLevel::High;
    return f;
}

driverse::core::DirectorySummary makeDir(const std::string& path,
                                         std::uintmax_t uniqueAlloc, int depth) {
    driverse::core::DirectorySummary d{};
    d.path = path;
    d.logicalSize = uniqueAlloc;
    d.allocatedSize = uniqueAlloc;
    d.uniqueAllocatedSize = uniqueAlloc;
    d.fileCount = 10;
    d.dirCount = 3;
    d.depth = static_cast<std::size_t>(depth);
    d.ownerUid = 1000;
    return d;
}

driverse::core::CleanupCandidate makeCandidate(const std::string& path,
                                               driverse::core::CleanupCategory cat,
                                               RiskLevel risk, std::uintmax_t alloc) {
    driverse::core::CleanupCandidate c;
    c.path = path;
    c.category = cat;
    c.logicalSize = alloc;
    c.allocatedSize = alloc;
    c.risk = risk;
    c.confidence = driverse::core::ConfidenceLevel::Medium;
    c.justification = "test justification";
    return c;
}

ScanDataPtr buildData() {
    auto sd = std::make_shared<ScanData>();
    sd->root = QStringLiteral("/home/u");

    // Files (largest first by allocated): bigfile 1000, med 500, t.jpg 100
    sd->result.largestFiles.push_back(makeFile("/home/u/.cache/a/bigfile.bin", 1000));
    sd->result.largestFiles.push_back(makeFile("/home/u/.cache/b/med.bin", 500));
    sd->result.largestFiles.push_back(makeFile("/home/u/.cache/thumbnails/t.jpg", 100));

    // Directories
    sd->result.directories.push_back(makeDir("/home/u/.cache", 2000, 1));
    sd->result.directories.push_back(makeDir("/home/u/Documents", 1500, 1));
    sd->result.directories.push_back(makeDir("/home/u/Downloads", 900, 1));

    // Recommendations
    sd->recommendations.push_back(
        makeCandidate("/home/u/.cache", driverse::core::CleanupCategory::ApplicationCache,
                      RiskLevel::Low, 2000));
    sd->recommendations.push_back(
        makeCandidate("/home/u/.cache/thumbnails",
                      driverse::core::CleanupCategory::ThumbnailCache, RiskLevel::Safe, 100));
    sd->recommendations.push_back(
        makeCandidate("/home/u/Downloads", driverse::core::CleanupCategory::Downloads,
                      RiskLevel::High, 1500));

    for (const auto& c : sd->recommendations) {
        sd->riskByPath.emplace(c.path.string(), c.risk);
    }
    // A recommended file (not covered by a recommended directory).
    sd->riskByPath.emplace("/home/u/.cache/thumbnails/t.jpg", RiskLevel::Safe);
    sd->totalRecoverable = 2000 + 100 + 1500;

    return sd;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << "Running scan model tests...\n";

    const ScanDataPtr sd = buildData();

    // ---- Files mode ----
    ResultsTableModel files(ResultsTableModel::Mode::Files);
    assert(files.rowCount() == 0);
    files.setData(sd);
    assert(files.rowCount() == 3);
    assert(files.columnCount() == ResultsTableModel::kColumnCount);
    assert(files.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString() == "Name");
    assert(files.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString() == "Path");

    // Default sort is allocated descending -> biggest file first.
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "bigfile.bin");

    files.sort(4, Qt::AscendingOrder);
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "t.jpg");
    files.sort(4, Qt::DescendingOrder);
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "bigfile.bin");

    // Path substring filter.
    files.setFilters(QStringLiteral("thumbnails"), 0, 0x1F, false);
    assert(files.rowCount() == 1);
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "t.jpg");

    // Minimum size filter (600 bytes) clears the path filter.
    files.setFilters(QString(), 600, 0x1F, false);
    assert(files.rowCount() == 1);
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "bigfile.bin");

    // Recommended-only: only the file with a risk entry survives.
    files.setFilters(QString(), 0, 0x1F, true);
    assert(files.rowCount() == 1);
    assert(files.data(files.index(0, 0), Qt::DisplayRole).toString() == "t.jpg");

    // Risk mask excluding Safe keeps the two unknown-risk files.
    files.setFilters(QString(), 0, 0x1E, false);
    assert(files.rowCount() == 2);

    files.clearFilters();
    assert(files.rowCount() == 3);

    // Path action + details.
    assert(!files.pathAt(0).isEmpty());
    assert(files.detailsText(0).contains(QStringLiteral("Path:")));
    assert(files.detailsHtml(0).contains(QStringLiteral("<table")));

    // Files are not checkable.
    assert(!(files.flags(files.index(0, 0)) & Qt::ItemIsUserCheckable));

    // ---- Directories mode ----
    ResultsTableModel dirs(ResultsTableModel::Mode::Directories);
    dirs.setData(sd);
    assert(dirs.rowCount() == 3);
    assert(dirs.headerData(2, Qt::Horizontal, Qt::DisplayRole).toString() == "Files");
    assert(dirs.headerData(4, Qt::Horizontal, Qt::DisplayRole).toString() == "Allocated");

    // ---- Recommendations mode ----
    ResultsTableModel recs(ResultsTableModel::Mode::Recommendations);
    recs.setData(sd);
    assert(recs.rowCount() == 3);
    assert(recs.headerData(2, Qt::Horizontal, Qt::DisplayRole).toString() == "Category");
    assert(recs.headerData(4, Qt::Horizontal, Qt::DisplayRole).toString() == "Recoverable");
    assert(recs.flags(recs.index(0, 0)) & Qt::ItemIsUserCheckable);
    assert(recs.checkedCount() == 0);

    // Risk mask excluding Safe hides the Safe candidate.
    recs.setFilters(QString(), 0, 0x1E, false);
    assert(recs.rowCount() == 2);
    recs.clearFilters();
    assert(recs.rowCount() == 3);

    // Default risk-ascending sort puts Safe first; select only the Safe group.
    recs.setVisibleChecked(true, static_cast<int>(RiskLevel::Safe));
    assert(recs.checkedCount() == 1);
    assert(recs.checkedRecoverable() == 100);

    // Select everything.
    recs.setVisibleChecked(true, -1);
    assert(recs.checkedCount() == 3);
    assert(recs.checkedCandidates().size() == 3);
    assert(recs.checkedRecoverable() == 2000 + 100 + 1500);

    // Clear.
    recs.setVisibleChecked(false, -1);
    assert(recs.checkedCount() == 0);

    // Toggle via the checkbox role.
    assert(recs.setData(recs.index(0, 0), Qt::Checked, Qt::CheckStateRole));
    assert(recs.checkedCount() == 1);
    assert(recs.data(recs.index(0, 0), Qt::CheckStateRole).toInt() == Qt::Checked);
    assert(recs.detailsText(0).contains(QStringLiteral("Justification")));

    // ---- Filter no-op ----
    // setFilters with unchanged values must not rebuild: scan completion
    // re-applies the same widget filters right after loading the data, and a
    // second full rebuild doubled the post-scan UI freeze on large scans.
    int resets = 0;
    QObject::connect(&files, &QAbstractItemModel::modelReset,
                     [&resets]() { ++resets; });
    files.setFilters(QString(), 0, 0x1F, false); // same as clearFilters() state
    assert(resets == 0);
    files.setFilters(QStringLiteral("big"), 0, 0x1F, false);
    assert(resets == 1);
    files.setFilters(QString(), 0, 0x1F, false);
    assert(resets == 2);
    assert(files.rowCount() == 3);

    // ---- Row cap ----
    // A directory list larger than the model cap keeps only the top rows
    // (default sort: largest first) instead of exposing (and sorting) all of
    // them.
    auto big = std::make_shared<ScanData>();
    big->root = QStringLiteral("/big");
    const std::size_t total = 200000 + 1; // kMaxRows + 1
    for (std::size_t i = 0; i < total; ++i) {
        big->result.directories.push_back(
            makeDir("/big/dir" + std::to_string(i), i, 1));
    }
    ResultsTableModel capped(ResultsTableModel::Mode::Directories);
    capped.setData(big);
    assert(capped.rowCount() == 200000);
    // Sizes are 0..200000: only the smallest (0-byte) entry is dropped.
    assert(capped.data(capped.index(0, 4), Qt::DisplayRole).toString() ==
           driverse::gui::formatting::bytes(200000));
    assert(capped.data(capped.index(199999, 4), Qt::DisplayRole).toString() ==
           driverse::gui::formatting::bytes(1));

    std::cout << "All scan model tests passed!\n";
    return 0;
}
