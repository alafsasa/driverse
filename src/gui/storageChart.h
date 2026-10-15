#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

namespace driverse::gui {

// Dependency-free storage utilization donut.
//
// Chosen over Qt Charts (see PROJECT_PLAN Q1): no extra dependency, keeps the
// build reproducible, and the device data we already have is enough.
class StorageChart : public QWidget {
    Q_OBJECT

public:
    explicit StorageChart(QWidget* parent = nullptr);

    // used/free in bytes; `caption` is shown under the percentage.
    void setUsage(std::uintmax_t usedBytes, std::uintmax_t freeBytes,
                  const QString& caption = QString());
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::uintmax_t m_used = 0;
    std::uintmax_t m_free = 0;
    QString m_caption;
    bool m_hasData = false;
};

} // namespace driverse::gui
