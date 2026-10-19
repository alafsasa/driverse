#include "scanworker.h"

#include "core/recommendations.h"
#include "core/types.h"

#include <QtConcurrent>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <vector>

namespace driverse::gui {

namespace {

std::filesystem::path homeDirectory() {
    const char* home = std::getenv("HOME");
    return home && *home ? std::filesystem::path(home) : std::filesystem::path("/");
}

} // namespace

ScanWorker::ScanWorker(QObject* parent) : QObject(parent) {}

ScanWorker::~ScanWorker() {
    if (m_future.isRunning()) {
        m_scanner.cancel();
        m_future.waitForFinished();
    }
}

void ScanWorker::start(const QString& root) {
    if (m_running.load()) {
        return; // one scan at a time
    }
    if (m_future.isRunning()) {
        // Defensive: previous task hasn't fully unwound yet.
        m_future.waitForFinished();
    }

    m_cancelRequested = false;

    core::ScanOptions options;
    options.root = std::filesystem::path(root.toStdString());
    options.oneFilesystem = true;
    options.skipProtectedPaths = true;
    options.measureAllocatedSize = true;
    m_scanner.setOptions(options);
    m_scanner.setProgressCallback([this](const core::ScanProgress& p) {
        emit progressChanged(static_cast<quint64>(p.filesScanned),
                             static_cast<quint64>(p.dirsScanned),
                             static_cast<quint64>(p.bytesSeen),
                             QString::fromStdString(p.currentPath.string()),
                             static_cast<quint64>(p.totalDirs), p.counting);
    });

    m_running = true;
    m_future = QtConcurrent::run([this, root]() {
        core::ScanResult res = m_scanner.scan();
        m_running = false;

        if (!res.error.empty()) {
            emit failed(QString::fromStdString(res.error));
            return;
        }

        // Classify candidates and build the immutable snapshot the views use.
        core::RecommendationOptions ropts;
        ropts.homeDirectory = homeDirectory();
        ropts.now = std::chrono::system_clock::now();
        core::RecommendationEngine engine(ropts);

        auto snapshot = std::make_shared<ScanData>();
        snapshot->root = root;
        snapshot->recommendations = engine.recommend(res);
        snapshot->totalRecoverable = core::totalRecoverable(snapshot->recommendations);
        for (const auto& candidate : snapshot->recommendations) {
            snapshot->riskByPath.emplace(candidate.path.string(), candidate.risk);
        }
        snapshot->result = std::move(res);

        const auto& stats = snapshot->result.stats;
        const quint64 files = static_cast<quint64>(stats.filesScanned);
        const quint64 dirs = static_cast<quint64>(stats.dirsScanned);
        const quint64 allocated = static_cast<quint64>(stats.totalAllocatedBytes);
        const quint64 unique = static_cast<quint64>(stats.uniqueAllocatedBytes);
        const quint64 skipped = static_cast<quint64>(stats.skippedCount);
        const quint64 durationMs = static_cast<quint64>(stats.duration.count());
        const quint64 recoverable = static_cast<quint64>(snapshot->totalRecoverable);
        const bool cancelled = snapshot->result.cancelled;

        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_data = snapshot;
        }

        emit completed(files, dirs, allocated, unique, skipped, durationMs, recoverable,
                       cancelled);
    });
}

ScanDataPtr ScanWorker::result() const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_data;
}

void ScanWorker::cancel() {
    m_cancelRequested = true;
    m_scanner.cancel();
}

} // namespace driverse::gui
