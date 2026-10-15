#pragma once

#include "core/scanner.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <vector>

namespace driverse::gui {

// Runs a filesystem scan on a background thread (QtConcurrent) and marshals
// progress/results to the GUI thread via queued signals. Keeps the UI responsive
// and supports cooperative cancellation.
class ScanWorker : public QObject {
    Q_OBJECT

public:
    explicit ScanWorker(QObject* parent = nullptr);
    ~ScanWorker() override;

    // Begin a scan of `root`. Non-blocking. Progress and result are delivered
    // asynchronously. Ignored while a scan is already running.
    void start(const QString& root);

    // Request cancellation of the in-flight scan (safe from any thread).
    void cancel();

    bool isRunning() const { return m_running.load(); }

    // Immediate children of the scan root, largest first (bounded). Snapshot
    // taken at the end of the most recent scan; safe to call from the GUI thread.
    std::vector<core::DirectorySummary> topLevelDirectories() const;

signals:
    // Emitted from a worker thread; queued to the receiver's thread.
    void progressChanged(quint64 files, quint64 dirs, quint64 bytes);
    void completed(quint64 filesScanned, quint64 dirsScanned, quint64 allocated,
                   quint64 uniqueAllocated, quint64 skippedCount, quint64 durationMs,
                   quint64 recoverableBytes, bool cancelled);
    void failed(const QString& error);

private:
    core::Scanner m_scanner;
    QFuture<void> m_future;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};

    mutable std::mutex m_resultMutex;
    std::vector<core::DirectorySummary> m_topDirs;
};

} // namespace driverse::gui
