#include "gui/scanworker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cassert>
#include <iostream>

using driverse::gui::ScanWorker;

// Integration test: drive a real scan through ScanWorker on a background thread
// and verify the assembled ScanData snapshot (no GUI/display required).
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << "Running scan worker tests...\n";

    QTemporaryDir dir;
    assert(dir.isValid());

    for (int i = 0; i < 3; ++i) {
        QFile file(dir.filePath(QStringLiteral("file%1.dat").arg(i)));
        assert(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1024 * (i + 1), 'x'));
        file.close();
    }

    ScanWorker worker;
    QEventLoop loop;
    bool finished = false;
    bool failed = false;

    QObject::connect(&worker, &ScanWorker::completed, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QObject::connect(&worker, &ScanWorker::failed, &loop, [&](const QString& error) {
        std::cerr << "scan failed: " << error.toStdString() << "\n";
        failed = true;
        loop.quit();
    });

    QTimer::singleShot(60000, &loop, &QEventLoop::quit);

    worker.start(dir.path());
    loop.exec();

    assert(finished);
    assert(!failed);
    assert(!worker.isRunning());

    const auto data = worker.result();
    assert(data != nullptr);
    assert(data->result.completed);
    assert(data->root == dir.path());
    assert(data->result.stats.filesScanned >= 3);
    assert(data->result.largestFiles.size() >= 3);

    // Every recommended path must be indexed for the Files/Dirs tabs.
    for (const auto& candidate : data->recommendations) {
        assert(data->riskByPath.count(candidate.path.string()) == 1);
    }

    // A nonexistent root (e.g. an unmounted path) must surface as a failure,
    // never as a silent empty "success".
    {
        ScanWorker failing;
        QEventLoop failLoop;
        bool failedSignal = false;
        bool completedSignal = false;
        QString error;
        QObject::connect(&failing, &ScanWorker::completed, &failLoop, [&]() {
            completedSignal = true;
            failLoop.quit();
        });
        QObject::connect(&failing, &ScanWorker::failed, &failLoop, [&](const QString& message) {
            failedSignal = true;
            error = message;
            failLoop.quit();
        });
        QTimer::singleShot(10000, &failLoop, &QEventLoop::quit);

        failing.start(dir.filePath(QStringLiteral("does-not-exist")));
        failLoop.exec();

        assert(failedSignal);
        assert(!completedSignal);
        assert(!error.isEmpty());
        assert(!failing.isRunning());
    }

    std::cout << "All scan worker tests passed!\n";
    return 0;
}
