#include "mainwindow.h"

#include "core/utils.h"
#include "platform/mounts.h"

#include "deviceTableModel.h"
#include "scanmodel.h"
#include "scanworker.h"
#include "storageChart.h"
#include "summaryCard.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace driverse::gui {

namespace {

QString bytes(std::uintmax_t value) {
    return QString::fromStdString(core::utils::formatBytes(value));
}

const char* const kRiskNames[5] = {"Safe", "Low", "Medium", "High", "Protected"};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Driverse - Linux Storage Cleaner"));
    resize(1200, 820);

    m_worker = new ScanWorker(this);

    buildMenus();
    buildCentral();

    connect(m_worker, &ScanWorker::progressChanged, this, &MainWindow::onProgress);
    connect(m_worker, &ScanWorker::completed, this, &MainWindow::onCompleted);
    connect(m_worker, &ScanWorker::failed, this, &MainWindow::onFailed);

    refreshDevices();
    updateRecsSummary();
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

    auto* mainSplit = new QSplitter(Qt::Vertical, central);
    auto* topSplit = new QSplitter(Qt::Horizontal, mainSplit);

    // ---- Left: device table ----
    auto* left = new QWidget(topSplit);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("<b>Filesystems</b>"), left));

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

    topSplit->addWidget(left);
    topSplit->addWidget(buildDashboard(topSplit));
    topSplit->setStretchFactor(0, 3);
    topSplit->setStretchFactor(1, 2);

    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(buildResultsPanel(mainSplit));
    mainSplit->setStretchFactor(0, 2);
    mainSplit->setStretchFactor(1, 3);
    rootLayout->addWidget(mainSplit, 1);

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

QWidget* MainWindow::buildDashboard(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);

    auto* cards = new QHBoxLayout();
    m_cardTotal = new SummaryCard(tr("TOTAL"), panel);
    m_cardUsed = new SummaryCard(tr("USED"), panel);
    m_cardFree = new SummaryCard(tr("FREE"), panel);
    m_cardRecoverable = new SummaryCard(tr("EST. RECOVERABLE"), panel);
    cards->addWidget(m_cardTotal);
    cards->addWidget(m_cardUsed);
    cards->addWidget(m_cardFree);
    cards->addWidget(m_cardRecoverable);
    layout->addLayout(cards);

    layout->addWidget(new QLabel(tr("<b>Storage usage</b>"), panel));
    m_chart = new StorageChart(panel);
    m_chart->setMinimumSize(200, 200);
    layout->addWidget(m_chart, 1);
    return panel;
}

QTableView* MainWindow::buildResultsView(QWidget* parent, ResultsTableModel* model) {
    auto* view = new QTableView(parent);
    view->setModel(model);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);
    view->verticalHeader()->setVisible(false);
    view->horizontalHeader()->setStretchLastSection(true);
    view->setSortingEnabled(true);

    const int defaultColumn =
        (model->mode() == ResultsTableModel::Mode::Recommendations) ? 3 : 4;
    const Qt::SortOrder defaultOrder =
        (model->mode() == ResultsTableModel::Mode::Recommendations) ? Qt::AscendingOrder
                                                                    : Qt::DescendingOrder;
    view->sortByColumn(defaultColumn, defaultOrder);

    wireResultsView(view, model);
    return view;
}

void MainWindow::wireResultsView(QTableView* view, ResultsTableModel* model) {
    connect(view, &QTableView::doubleClicked, this, [this, model](const QModelIndex& index) {
        if (index.isValid()) {
            openContainingFolder(model->pathAt(index.row()));
        }
    });
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested, this,
            [this, view, model](const QPoint& pos) {
                showResultsContextMenu(view, model, pos);
            });
    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { updateDetails(); });
}

QWidget* MainWindow::buildResultsPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);

    // ---- Filter bar ----
    auto* filters = new QHBoxLayout();
    filters->addWidget(new QLabel(tr("Path:"), panel));
    m_pathFilter = new QLineEdit(panel);
    m_pathFilter->setPlaceholderText(tr("substring..."));
    m_pathFilter->setClearButtonEnabled(true);
    m_pathFilter->setMaximumWidth(240);
    filters->addWidget(m_pathFilter);

    filters->addWidget(new QLabel(tr("Min size:"), panel));
    m_minSize = new QDoubleSpinBox(panel);
    m_minSize->setRange(0.0, 1.0e9);
    m_minSize->setDecimals(1);
    m_minSize->setSuffix(tr(" MiB"));
    m_minSize->setMaximumWidth(120);
    filters->addWidget(m_minSize);

    for (int i = 0; i < 5; ++i) {
        m_riskChecks[static_cast<std::size_t>(i)] =
            new QCheckBox(QString::fromLatin1(kRiskNames[i]), panel);
        m_riskChecks[static_cast<std::size_t>(i)]->setChecked(true);
        filters->addWidget(m_riskChecks[static_cast<std::size_t>(i)]);
    }
    m_recommendedOnly = new QCheckBox(tr("Recommended only"), panel);
    filters->addWidget(m_recommendedOnly);
    filters->addStretch(1);
    layout->addLayout(filters);

    // ---- Tabs + details ----
    auto* body = new QHBoxLayout();
    m_tabs = new QTabWidget(panel);

    m_filesModel = new ResultsTableModel(ResultsTableModel::Mode::Files, this);
    m_filesView = buildResultsView(m_tabs, m_filesModel);
    m_tabs->addTab(m_filesView, tr("Largest Files"));

    m_dirsModel = new ResultsTableModel(ResultsTableModel::Mode::Directories, this);
    m_dirsView = buildResultsView(m_tabs, m_dirsModel);
    m_tabs->addTab(m_dirsView, tr("Largest Directories"));

    m_recsModel = new ResultsTableModel(ResultsTableModel::Mode::Recommendations, this);
    auto* recsTab = new QWidget(m_tabs);
    auto* recsLayout = new QVBoxLayout(recsTab);
    recsLayout->setContentsMargins(0, 0, 0, 0);

    auto* recsBar = new QHBoxLayout();
    auto* selectGroup = new QPushButton(tr("Select group"), recsTab);
    auto* selectAll = new QPushButton(tr("Select all shown"), recsTab);
    auto* clearSel = new QPushButton(tr("Clear selection"), recsTab);
    recsBar->addWidget(new QLabel(tr("Group:"), recsTab));
    m_groupCombo = new QComboBox(recsTab);
    m_groupCombo->addItems({tr("Safe"), tr("Low"), tr("Medium"), tr("High"),
                            tr("Protected")});
    recsBar->addWidget(m_groupCombo);
    recsBar->addWidget(selectGroup);
    recsBar->addWidget(selectAll);
    recsBar->addWidget(clearSel);
    recsBar->addStretch(1);
    m_selectionSummary = new QLabel(tr("0 selected"), recsTab);
    recsBar->addWidget(m_selectionSummary);
    recsLayout->addLayout(recsBar);

    m_recsView = buildResultsView(recsTab, m_recsModel);
    recsLayout->addWidget(m_recsView, 1);
    m_tabs->addTab(recsTab, tr("Recommendations"));

    body->addWidget(m_tabs, 1);
    m_details = new QTextBrowser(panel);
    m_details->setMinimumWidth(280);
    m_details->setMaximumWidth(380);
    m_details->setHtml(tr("<p style='color:#888'>Select a row to see details.</p>"));
    body->addWidget(m_details);
    layout->addLayout(body, 1);

    // ---- Filter wiring (debounced) ----
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(200);
    connect(m_filterTimer, &QTimer::timeout, this, &MainWindow::applyFilters);

    auto schedule = [this]() { m_filterTimer->start(); };
    connect(m_pathFilter, &QLineEdit::textChanged, this, schedule);
    connect(m_minSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, schedule);
    connect(m_recommendedOnly, &QCheckBox::toggled, this, schedule);
    for (QCheckBox* check : m_riskChecks) {
        connect(check, &QCheckBox::toggled, this, schedule);
    }

    connect(selectGroup, &QPushButton::clicked, this, &MainWindow::selectRiskGroup);
    connect(selectAll, &QPushButton::clicked, this, &MainWindow::selectAllRecommendations);
    connect(clearSel, &QPushButton::clicked, this, &MainWindow::clearRecommendations);
    connect(m_recsModel, &ResultsTableModel::checkedChanged, this,
            &MainWindow::onRecsCheckedChanged);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    return panel;
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
    Q_UNUSED(allocated);

    const QString prefix = cancelled ? tr("Scan cancelled") : tr("Scan complete");
    m_status->setText(tr("%1: %2 files, %3 dirs, %4 (allocated), %5 skipped, %6 ms")
                          .arg(prefix)
                          .arg(filesScanned)
                          .arg(dirsScanned)
                          .arg(bytes(uniqueAllocated))
                          .arg(skippedCount)
                          .arg(durationMs));

    m_cardRecoverable->setValue(bytes(recoverableBytes));
    m_cardRecoverable->setAccent(true);

    const ScanDataPtr data = m_worker->result();
    m_filesModel->setData(data);
    m_dirsModel->setData(data);
    m_recsModel->setData(data);
    applyFilters();

    m_tabs->setCurrentIndex(0);
    updateDetails();
    updateRecsSummary();
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

void MainWindow::applyFilters() {
    if (!m_filesModel || !m_minSize || !m_pathFilter) {
        return;
    }
    const QString path = m_pathFilter->text();
    const auto minBytes = static_cast<std::uintmax_t>(m_minSize->value() * 1024.0 * 1024.0);
    int mask = 0;
    for (int i = 0; i < 5; ++i) {
        if (m_riskChecks[static_cast<std::size_t>(i)]->isChecked()) {
            mask |= (1 << i);
        }
    }
    const bool recOnly = m_recommendedOnly->isChecked();

    m_filesModel->setFilters(path, minBytes, mask, recOnly);
    m_dirsModel->setFilters(path, minBytes, mask, recOnly);
    m_recsModel->setFilters(path, minBytes, mask, recOnly);

    updateDetails();
    updateRecsSummary();
}

void MainWindow::onTabChanged() {
    updateDetails();
}

void MainWindow::updateDetails() {
    if (!m_details) {
        return;
    }
    QTableView* view = activeResultsView();
    if (!view) {
        m_details->clear();
        return;
    }
    const QModelIndex index = view->currentIndex();
    auto* model = static_cast<ResultsTableModel*>(view->model());
    if (!index.isValid()) {
        m_details->setHtml(tr("<p style='color:#888'>Select a row to see details.</p>"));
        return;
    }
    m_details->setHtml(model->detailsHtml(index.row()));
}

void MainWindow::onRecsCheckedChanged() {
    updateRecsSummary();
}

void MainWindow::updateRecsSummary() {
    if (!m_selectionSummary || !m_recsModel) {
        return;
    }
    const int count = m_recsModel->checkedCount();
    const std::uintmax_t recoverable = m_recsModel->checkedRecoverable();
    m_selectionSummary->setText(
        tr("%1 selected — %2 recoverable").arg(count).arg(bytes(recoverable)));
}

void MainWindow::selectRiskGroup() {
    if (!m_groupCombo || !m_recsModel) {
        return;
    }
    m_recsModel->setVisibleChecked(true, m_groupCombo->currentIndex());
}

void MainWindow::selectAllRecommendations() {
    if (m_recsModel) {
        m_recsModel->setVisibleChecked(true, -1);
    }
}

void MainWindow::clearRecommendations() {
    if (m_recsModel) {
        m_recsModel->setVisibleChecked(false, -1);
    }
}

QTableView* MainWindow::activeResultsView() const {
    if (!m_tabs) {
        return nullptr;
    }
    switch (m_tabs->currentIndex()) {
        case 0: return m_filesView;
        case 1: return m_dirsView;
        case 2: return m_recsView;
        default: return nullptr;
    }
}

ResultsTableModel* MainWindow::activeResultsModel() const {
    QTableView* view = activeResultsView();
    return view ? static_cast<ResultsTableModel*>(view->model()) : nullptr;
}

void MainWindow::showResultsContextMenu(QTableView* view, ResultsTableModel* model,
                                        const QPoint& pos) {
    const QModelIndex index = view->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    QMenu menu(this);
    QAction* openAction = menu.addAction(tr("Open containing folder"));
    QAction* copyPathAction = menu.addAction(tr("Copy path"));
    QAction* copyDetailsAction = menu.addAction(tr("Copy details"));
    QAction* chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == openAction) {
        openContainingFolder(model->pathAt(index.row()));
    } else if (chosen == copyPathAction) {
        QApplication::clipboard()->setText(model->pathAt(index.row()));
    } else if (chosen == copyDetailsAction) {
        QApplication::clipboard()->setText(model->detailsText(index.row()));
    }
}

void MainWindow::openContainingFolder(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (dir.isEmpty()) {
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        m_status->setText(tr("Could not open %1").arg(dir));
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
