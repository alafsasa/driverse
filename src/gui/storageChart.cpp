#include "storageChart.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace driverse::gui {

namespace {

const QColor kFreeRing(228, 228, 232);
const QColor kUsedRing(56, 142, 206);
const QColor kUsedRingHigh(200, 70, 70);
const QColor kTextColor(60, 60, 66);
const QColor kSubColor(120, 120, 128);

} // namespace

StorageChart::StorageChart(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void StorageChart::setUsage(std::uintmax_t usedBytes, std::uintmax_t freeBytes,
                            const QString& caption) {
    m_used = usedBytes;
    m_free = freeBytes;
    m_caption = caption;
    m_hasData = true;
    update();
}

void StorageChart::clear() {
    m_used = 0;
    m_free = 0;
    m_caption.clear();
    m_hasData = false;
    update();
}

QSize StorageChart::sizeHint() const { return QSize(220, 220); }
QSize StorageChart::minimumSizeHint() const { return QSize(120, 120); }

void StorageChart::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal side = std::min(width(), height());
    const qreal margin = side * 0.06;
    const QRectF rect((width() - side) / 2.0 + margin, (height() - side) / 2.0 + margin,
                      side - 2 * margin, side - 2 * margin);

    const std::uintmax_t total = m_used + m_free;
    double fraction = 0.0;
    if (total > 0) {
        fraction = static_cast<double>(m_used) / static_cast<double>(total);
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
    }

    const qreal penWidth = side * 0.14;
    QPen ringPen(kFreeRing);
    ringPen.setWidthF(penWidth);
    ringPen.setCapStyle(Qt::FlatCap);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(rect);

    if (m_hasData && fraction > 0.0) {
        ringPen.setColor(fraction >= 0.9 ? kUsedRingHigh : kUsedRing);
        painter.setPen(ringPen);
        // Qt angles are in 1/16th of a degree, 0 = 3 o'clock, positive =
        // counter-clockwise. Start at 12 o'clock and sweep clockwise.
        const int start = 90 * 16;
        const int span = -static_cast<int>(std::lround(fraction * 360.0 * 16.0));
        painter.drawArc(rect, start, span);
    }

    // Percentage in the upper half of the hole, caption below.
    QFont pctFont = font();
    pctFont.setPixelSize(side * 0.24);
    pctFont.setBold(true);
    painter.setFont(pctFont);
    painter.setPen(kTextColor);

    QRectF upper = rect;
    upper.setBottom(rect.center().y());
    const QString pct = m_hasData
        ? QString::number(fraction * 100.0, 'f', 0) + QStringLiteral("%")
        : QStringLiteral("\u2013");
    painter.drawText(upper, Qt::AlignHCenter | Qt::AlignVCenter, pct);

    if (!m_caption.isEmpty()) {
        QFont capFont = font();
        capFont.setPixelSize(side * 0.085);
        capFont.setBold(false);
        painter.setFont(capFont);
        painter.setPen(kSubColor);
        QRectF lower = rect;
        lower.setTop(rect.center().y());
        painter.drawText(lower, Qt::AlignHCenter | Qt::AlignVCenter, m_caption);
    }
}

} // namespace driverse::gui
