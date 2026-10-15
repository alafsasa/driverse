#pragma once

#include "core/types.h"

#include <QMainWindow>
#include <QString>

#include <vector>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableView;

namespace driverse::gui {

class DeviceTableModel;
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

private:
    void buildMenus();
    void buildCentral();
    void setScanning(bool scanning);
    void updateDashboardForDevice(const core::DeviceInfo* device);
    QString currentTarget() const;

    DeviceTableModel* m_model = nullptr;
    QTableView* m_table = nullptr;

    StorageChart* m_chart = nullptr;
    SummaryCard* m_cardTotal = nullptr;
    SummaryCard* m_cardUsed = nullptr;
    SummaryCard* m_cardFree = nullptr;
    SummaryCard* m_cardRecoverable = nullptr;
    QListWidget* m_topDirs = nullptr;
    QLabel* m_targetLabel = nullptr;

    QPushButton* m_scanButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_selectButton = nullptr;

    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;

    ScanWorker* m_worker = nullptr;

    std::vector<core::DeviceInfo> m_devices;
    QString m_selectedFolder; // explicit folder target, if chosen
};

} // namespace driverse::gui
