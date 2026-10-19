#pragma once

#include "core/scanner.h"
#include "scandata.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>

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

    // Full snapshot of the most recent scan (files, directories, recommendations).
    // Returns null before the first scan finishes. Safe to call from the GUI
    // thread once `completed` has been emitted.
    ScanDataPtr result() const;

signals:
    // Emitted from a worker thread; queued to the receiver's thread. The
    // currentPath argument is the path being visited (for progress display and
    // stall diagnostics); totalDirs is the G3 pre-count total (0 = unknown) and
    // counting is true while that pre-count pass runs.
    void progressChanged(quint64 files, quint64 dirs, quint64 bytes,
                         const QString& currentPath, quint64 totalDirs,
                         bool counting);
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
    ScanDataPtr m_data;
};

} // namespace driverse::gui
