#pragma once

#include "core/types.h"

#include <QMainWindow>
#include <QString>

#include <array>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableView;
class QTextBrowser;
class QTimer;

namespace driverse::gui {

class DeviceTableModel;
class ResultsTableModel;
class ScanWorker;
class StorageChart;
class SummaryCard;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void refreshDevices();
    void onDeviceSelectionChanged();
    void chooseFolder();
    void startScan();
    void cancelScan();

    void onProgress(quint64 files, quint64 dirs, quint64 bytes);
    void onCompleted(quint64 filesScanned, quint64 dirsScanned, quint64 allocated,
                     quint64 uniqueAllocated, quint64 skippedCount, quint64 durationMs,
                     quint64 recoverableBytes, bool cancelled);
    void onFailed(const QString& error);
    void showAbout();
    void showSkippedPaths();

    void applyFilters();
    void onTabChanged();
    void updateDetails();
    void onRecsCheckedChanged();
    void selectRiskGroup();
    void selectAllRecommendations();
    void clearRecommendations();
    void startCleanup();

private:
    void buildMenus();
    void buildCentral();
    QWidget* buildDashboard(QWidget* parent);
    QWidget* buildResultsPanel(QWidget* parent);
    QTableView* buildResultsView(QWidget* parent, ResultsTableModel* model);
    void wireResultsView(QTableView* view, ResultsTableModel* model);
    void showResultsContextMenu(QTableView* view, ResultsTableModel* model,
                                const QPoint& pos);
    void openContainingFolder(const QString& path);
    void setScanning(bool scanning);
    void updateDashboardForDevice(const core::DeviceInfo* device);
    void updateRecsSummary();
    void updateEmptyState();
    const core::DeviceInfo* selectedDevice() const;
    QTableView* activeResultsView() const;
    ResultsTableModel* activeResultsModel() const;
    QString currentTarget() const;

    DeviceTableModel* m_model = nullptr;
    QTableView* m_table = nullptr;

    StorageChart* m_chart = nullptr;
    SummaryCard* m_cardTotal = nullptr;
    SummaryCard* m_cardUsed = nullptr;
    SummaryCard* m_cardFree = nullptr;
    SummaryCard* m_cardRecoverable = nullptr;
    QLabel* m_targetLabel = nullptr;

    QPushButton* m_scanButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_selectButton = nullptr;

    // Results view
    QTabWidget* m_tabs = nullptr;
    QStackedWidget* m_resultsStack = nullptr;
    QLabel* m_emptyStateLabel = nullptr;
    QTableView* m_filesView = nullptr;
    QTableView* m_dirsView = nullptr;
    QTableView* m_recsView = nullptr;
    ResultsTableModel* m_filesModel = nullptr;
    ResultsTableModel* m_dirsModel = nullptr;
    ResultsTableModel* m_recsModel = nullptr;
    QTextBrowser* m_details = nullptr;

    // Filters
    QTimer* m_filterTimer = nullptr;
    QLineEdit* m_pathFilter = nullptr;
    QDoubleSpinBox* m_minSize = nullptr;
    QCheckBox* m_recommendedOnly = nullptr;
    std::array<QCheckBox*, 5> m_riskChecks{};

    // Recommendation selection controls
    QComboBox* m_groupCombo = nullptr;
    QLabel* m_selectionSummary = nullptr;
    QPushButton* m_cleanupButton = nullptr;

    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QAction* m_skippedAction = nullptr;

    ScanWorker* m_worker = nullptr;

    std::vector<core::DeviceInfo> m_devices;
    QString m_selectedFolder; // explicit folder target, if chosen
};

} // namespace driverse::gui
