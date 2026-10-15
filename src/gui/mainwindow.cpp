#include "mainwindow.h"

#include "core/utils.h"
#include "platform/mounts.h"

#include "deviceTableModel.h"
#include "scanworker.h"
#include "storageChart.h"
#include "summaryCard.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

namespace driverse::gui {

namespace {

QString bytes(std::uintmax_t value) {
    return QString::fromStdString(core::utils::formatBytes(value));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Driverse - Linux Storage Cleaner"));
    resize(1100, 720);

    m_worker = new ScanWorker(this);

    buildMenus();
    buildCentral();

    connect(m_worker, &ScanWorker::progressChanged, this, &MainWindow::onProgress);
    connect(m_worker, &ScanWorker::completed, this, &MainWindow::onCompleted);
    connect(m_worker, &ScanWorker::failed, this, &MainWindow::onFailed);

    refreshDevices();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* scanMenu = menuBar()->addMenu(tr("&Scan"));
    auto* folderAction = scanMenu->addAction(tr("Select &Folder..."));
    folderAction->setShortcut(QKeySequence::Open);
    connect(folderAction, &QAction::triggered, this, &MainWindow::chooseFolder);

    auto* startAction = scanMenu->addAction(tr("&Start Scan"));
    startAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(startAction, &QAction::triggered, this, &MainWindow::startScan);

    auto* cancelAction = scanMenu->addAction(tr("&Cancel"));
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, &MainWindow::cancelScan);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About Driverse"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildCentral() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    // ---- Scan control bar ----
    auto* controls = new QHBoxLayout();
    m_targetLabel = new QLabel(tr("No target selected"), central);
    m_targetLabel->setStyleSheet(QStringLiteral("color: #55555c;"));
    m_selectButton = new QPushButton(tr("Select Folder..."), central);
    m_scanButton = new QPushButton(tr("Scan"), central);
    m_cancelButton = new QPushButton(tr("Cancel"), central);
    m_cancelButton->setEnabled(false);
    controls->addWidget(m_targetLabel, 1);
    controls->addWidget(m_selectButton);
    controls->addWidget(m_scanButton);
    controls->addWidget(m_cancelButton);
    rootLayout->addLayout(controls);

    auto* splitter = new QSplitter(Qt::Horizontal, central);

    // ---- Left: device table ----
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    auto* fsLabel = new QLabel(tr("<b>Filesystems</b>"), left);
    leftLayout->addWidget(fsLabel);

    m_table = new QTableView(left);
    m_model = new DeviceTableModel(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    leftLayout->addWidget(m_table, 1);

    auto* refreshButton = new QPushButton(tr("Refresh"), left);
    leftLayout->addWidget(refreshButton);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDevices);

    // ---- Right: dashboard ----
    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);

    auto* cards = new QHBoxLayout();
    m_cardTotal = new SummaryCard(tr("TOTAL"), right);
    m_cardUsed = new SummaryCard(tr("USED"), right);
    m_cardFree = new SummaryCard(tr("FREE"), right);
    m_cardRecoverable = new SummaryCard(tr("EST. RECOVERABLE"), right);
    cards->addWidget(m_cardTotal);
    cards->addWidget(m_cardUsed);
    cards->addWidget(m_cardFree);
    cards->addWidget(m_cardRecoverable);
    rightLayout->addLayout(cards);

    auto* mid = new QHBoxLayout();
    m_chart = new StorageChart(right);
    m_chart->setMinimumSize(180, 180);
    mid->addWidget(m_chart, 1);

    m_topDirs = new QListWidget(right);
    m_topDirs->setAlternatingRowColors(true);
    mid->addWidget(m_topDirs, 1);
    rightLayout->addLayout(mid, 3);

    rightLayout->addWidget(new QLabel(tr("<b>Largest directories (after scan)</b>"), right));

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);

    // ---- Connections ----
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onDeviceSelectionChanged);
    connect(m_selectButton, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancelScan);

    // ---- Status bar ----
    m_status = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(m_status, 1);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedWidth(180);
    statusBar()->addPermanentWidget(m_progress);
}

void MainWindow::refreshDevices() {
    m_devices = platform::MountDetector::getRelevantMounts();
    m_model->setDevices(m_devices);

    if (!m_devices.empty()) {
        m_table->selectRow(0);
    } else {
        updateDashboardForDevice(nullptr);
    }
    m_status->setText(tr("Loaded %1 filesystem(s)").arg(m_devices.size()));
}

void MainWindow::onDeviceSelectionChanged() {
    const QModelIndex current = m_table->currentIndex();
    const core::DeviceInfo* device = m_model->deviceAt(current.row());
    updateDashboardForDevice(device);

    // Selecting a device makes it the scan target unless a folder was chosen.
    if (!m_selectedFolder.isEmpty()) {
        m_targetLabel->setText(tr("Target: %1").arg(m_selectedFolder));
    } else if (device) {
        m_targetLabel->setText(tr("Target: %1").arg(QString::fromStdString(device->mountPoint)));
    }
    m_scanButton->setEnabled(!currentTarget().isEmpty());
}

void MainWindow::updateDashboardForDevice(const core::DeviceInfo* device) {
    if (!device) {
        m_chart->clear();
        m_cardTotal->setValue(QStringLiteral("--"));
        m_cardUsed->setValue(QStringLiteral("--"));
        m_cardFree->setValue(QStringLiteral("--"));
        return;
    }
    m_cardTotal->setValue(bytes(device->totalBytes));
    m_cardUsed->setValue(bytes(device->usedBytes));
    m_cardFree->setValue(bytes(device->availableBytes));
    m_chart->setUsage(device->usedBytes, device->availableBytes,
                      QString::fromStdString(device->mountPoint));
}

QString MainWindow::currentTarget() const {
    if (!m_selectedFolder.isEmpty()) {
        return m_selectedFolder;
    }
    const QModelIndex current = m_table->currentIndex();
    const core::DeviceInfo* device = m_model->deviceAt(current.row());
    return device ? QString::fromStdString(device->mountPoint) : QString();
}

void MainWindow::chooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select folder to scan"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }
    m_selectedFolder = dir;
    m_targetLabel->setText(tr("Target: %1").arg(dir));
    m_scanButton->setEnabled(true);
}

void MainWindow::startScan() {
    if (m_worker->isRunning()) {
        return;
    }
    const QString target = currentTarget();
    if (target.isEmpty()) {
        QMessageBox::information(this, tr("No target"),
                                 tr("Select a filesystem or folder to scan first."));
        return;
    }

    m_topDirs->clear();
    m_cardRecoverable->setValue(QStringLiteral("..."));
    m_status->setText(tr("Scanning %1 ...").arg(target));
    setScanning(true);
    m_worker->start(target);
}

void MainWindow::cancelScan() {
    if (m_worker->isRunning()) {
        m_status->setText(tr("Cancelling ..."));
        m_worker->cancel();
    }
}

void MainWindow::onProgress(quint64 files, quint64 dirs, quint64 seenBytes) {
    m_status->setText(tr("Scanning... %1 files, %2 dirs, %3")
                          .arg(files)
                          .arg(dirs)
                          .arg(bytes(seenBytes)));
}

void MainWindow::onCompleted(quint64 filesScanned, quint64 dirsScanned, quint64 allocated,
                             quint64 uniqueAllocated, quint64 skippedCount,
                             quint64 durationMs, quint64 recoverableBytes, bool cancelled) {
    setScanning(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(100);

    const QString prefix = cancelled ? tr("Scan cancelled") : tr("Scan complete");
    m_status->setText(tr("%1: %2 files, %3 dirs, %4 (allocated), %5 skipped, %6 ms")
                          .arg(prefix)
                          .arg(filesScanned)
                          .arg(dirsScanned)
                          .arg(bytes(uniqueAllocated))
                          .arg(skippedCount)
                          .arg(durationMs));
    Q_UNUSED(allocated);

    m_cardRecoverable->setValue(bytes(recoverableBytes));
    m_cardRecoverable->setAccent(true);

    m_topDirs->clear();
    for (const auto& dir : m_worker->topLevelDirectories()) {
        const QString label = tr("%1  —  %2")
                                  .arg(bytes(dir.uniqueAllocatedSize),
                                       QString::fromStdString(dir.path.string()));
        m_topDirs->addItem(label);
    }
    if (m_topDirs->count() == 0) {
        m_topDirs->addItem(tr("(no subdirectories)"));
    }
}

void MainWindow::onFailed(const QString& error) {
    setScanning(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_cardRecoverable->setValue(QStringLiteral("--"));
    m_status->setText(tr("Scan failed: %1").arg(error));
    QMessageBox::warning(this, tr("Scan failed"), error);
}

void MainWindow::setScanning(bool scanning) {
    m_scanButton->setEnabled(!scanning && !currentTarget().isEmpty());
    m_cancelButton->setEnabled(scanning);
    m_selectButton->setEnabled(!scanning);
    if (scanning) {
        m_progress->setRange(0, 0); // busy indicator (no total estimate yet: G3)
        m_progress->setValue(0);
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About Driverse"),
        tr("<h3>Driverse</h3>"
           "<p>A safe, conservative Linux storage cleaner.</p>"
           "<p>Scanning never deletes anything. Recommendations are "
           "conservative, system paths are protected, and destructive actions "
           "always require explicit confirmation and a dry-run.</p>"
           "<p>Version 1.0.0</p>"));
}

} // namespace driverse::gui
