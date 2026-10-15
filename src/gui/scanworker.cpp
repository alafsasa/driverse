#include "scanworker.h"

#include "core/recommendations.h"
#include "core/types.h"

#include <QtConcurrent>

#include <algorithm>
#include <cstdlib>

namespace driverse::gui {

namespace {

constexpr std::size_t kTopDirLimit = 12;

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
                             static_cast<quint64>(p.bytesSeen));
    });

    m_running = true;
    m_future = QtConcurrent::run([this]() {
        const core::ScanResult res = m_scanner.scan();
        m_running = false;

        // Snapshot immediate children of the root (depth == 1), largest first.
        {
            std::vector<core::DirectorySummary> top;
            for (const auto& d : res.directories) {
                if (d.depth == 1) {
                    top.push_back(d);
                }
            }
            std::sort(top.begin(), top.end(),
                      [](const core::DirectorySummary& a, const core::DirectorySummary& b) {
                          return a.uniqueAllocatedSize > b.uniqueAllocatedSize;
                      });
            if (top.size() > kTopDirLimit) {
                top.resize(kTopDirLimit);
            }
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_topDirs = std::move(top);
        }

        if (!res.error.empty()) {
            emit failed(QString::fromStdString(res.error));
            return;
        }

        // Conservative recoverable estimate from the recommendation engine.
        std::uintmax_t recoverable = 0;
        if (res.completed || res.cancelled) {
            core::RecommendationOptions ropts;
            ropts.homeDirectory = homeDirectory();
            ropts.now = std::chrono::system_clock::now();
            core::RecommendationEngine engine(ropts);
            recoverable = core::totalRecoverable(engine.recommend(res));
        }

        emit completed(static_cast<quint64>(res.stats.filesScanned),
                       static_cast<quint64>(res.stats.dirsScanned),
                       static_cast<quint64>(res.stats.totalAllocatedBytes),
                       static_cast<quint64>(res.stats.uniqueAllocatedBytes),
                       static_cast<quint64>(res.stats.skippedCount),
                       static_cast<quint64>(res.stats.duration.count()),
                       static_cast<quint64>(recoverable),
                       res.cancelled);
    });
}

std::vector<core::DirectorySummary> ScanWorker::topLevelDirectories() const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_topDirs;
}

void ScanWorker::cancel() {
    m_cancelRequested = true;
    m_scanner.cancel();
}

} // namespace driverse::gui
