#pragma once

#include "core/types.h"
#include "core/utils.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>
#include <QString>

#include <chrono>
#include <cstdint>

namespace driverse::gui::formatting {

// Locale-aware human-readable size using IEC units (B, KiB, MiB, GiB, ...).
inline QString bytes(std::uintmax_t value) {
    if (value == 0) {
        return QStringLiteral("0 B");
    }
    return QLocale().formattedDataSize(static_cast<qlonglong>(value), 1,
                                       QLocale::DataSizeIecFormat);
}

// Locale-aware short date/time; an em dash stands for "no timestamp".
inline QString timeToString(const std::chrono::system_clock::time_point& tp) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    if (secs.count() <= 0) {
        return QStringLiteral("\u2014");
    }
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs.count()));
    return QLocale().toString(dt, QLocale::ShortFormat);
}

// Single source of truth for risk colours (tables, dialogs, details panel).
inline QColor riskColor(core::RiskLevel risk) {
    switch (risk) {
        case core::RiskLevel::Safe:            return QColor(45, 143, 78);
        case core::RiskLevel::Low:             return QColor(45, 128, 160);
        case core::RiskLevel::Medium:          return QColor(191, 138, 32);
        case core::RiskLevel::High:            return QColor(200, 70, 60);
        case core::RiskLevel::SystemProtected: return QColor(120, 60, 150);
    }
    return QColor(90, 90, 96);
}

inline QString riskName(core::RiskLevel risk) {
    return QString::fromStdString(core::utils::riskLevelToString(risk));
}

} // namespace driverse::gui::formatting
