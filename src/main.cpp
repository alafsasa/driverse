#include "gui/mainwindow.h"

#include "core/utils.h"
#include "utils/config.h"
#include "utils/integration.h"
#include "utils/logger.h"

#include <QApplication>

#include <cstdlib>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Driverse"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setOrganizationName(QStringLiteral("driverse"));

    // Load user config (falls back to built-in defaults) and apply custom
    // protected paths so the UI and scanner honour them.
    const std::filesystem::path configPath = driverse::utils::defaultConfigPath();
    driverse::utils::Config config = driverse::utils::Config::load(configPath);
    driverse::utils::applyProtectedPaths(config, driverse::core::Protections::instance());

    if (const char* home = std::getenv("HOME")) {
        driverse::utils::Logger::instance().setLogFile(
            std::filesystem::path(home) / ".local" / "share" / "driverse" / "driverse.log");
    }

    driverse::gui::MainWindow window;
    window.show();
    return app.exec();
}
