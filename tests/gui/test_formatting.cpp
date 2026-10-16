#include "gui/formatting.h"

#include <QCoreApplication>
#include <QSet>

#include <cassert>
#include <chrono>
#include <iostream>

using driverse::core::RiskLevel;
namespace formatting = driverse::gui::formatting;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << "Running formatting tests...\n";

    // One canonical path for the sizes used across the UI.
    assert(formatting::bytes(0) == QStringLiteral("0 B"));
    assert(formatting::bytes(1024).contains(QStringLiteral("KiB")));
    assert(formatting::bytes(1536).startsWith(QStringLiteral("1")));
    assert(formatting::bytes(1024ULL * 1024).contains(QStringLiteral("MiB")));
    assert(formatting::bytes(3ULL * 1024 * 1024 * 1024).contains(QStringLiteral("GiB")));

    // Epoch zero means "no timestamp" and renders as an em dash.
    assert(formatting::timeToString(std::chrono::system_clock::time_point{}) ==
           QStringLiteral("\u2014"));
    assert(!formatting::timeToString(std::chrono::system_clock::now()).isEmpty());

    // Risk palette: five distinct colours, stable names.
    QSet<QString> colors;
    for (int i = 0; i <= static_cast<int>(RiskLevel::SystemProtected); ++i) {
        colors.insert(formatting::riskColor(static_cast<RiskLevel>(i)).name());
    }
    assert(colors.size() == 5);
    assert(formatting::riskName(RiskLevel::Safe) == QStringLiteral("Safe"));
    assert(formatting::riskName(RiskLevel::SystemProtected) ==
           QStringLiteral("System Protected"));

    std::cout << "All formatting tests passed!\n";
    return 0;
}
