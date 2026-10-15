#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace driverse::utils {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

std::string toString(LogLevel level);

// Minimal thread-safe logger with a minimum level and optional file sink.
//
// - Messages below the minimum level are dropped.
// - By default, messages are written to stderr with a timestamp and level.
// - An optional file sink receives the same lines (append-only).
// - Paths should be passed through core::utils::abbreviatePath to avoid
//   logging long/sensitive full paths.
class Logger {
public:
    static Logger& instance(); // app-wide logger (default level: Info)

    Logger() = default;

    void setMinLevel(LogLevel level);
    LogLevel minLevel() const;

    // Enable/disable file sink (empty path disables).
    bool setLogFile(const std::filesystem::path& path);
    const std::filesystem::path& logFile() const;

    // Mirror log lines to stderr/stdout.
    void setConsole(bool enabled);

    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message) { log(LogLevel::Debug, message); }
    void info(const std::string& message)  { log(LogLevel::Info, message); }
    void warn(const std::string& message)  { log(LogLevel::Warn, message); }
    void error(const std::string& message) { log(LogLevel::Error, message); }

    // Format a timestamp for log lines: "YYYY-MM-DDTHH:MM:SSZ".
    static std::string timestamp();

private:
    LogLevel m_minLevel = LogLevel::Info;
    bool m_console = true;
    std::filesystem::path m_logFile;
    mutable std::mutex m_mutex;
};

} // namespace driverse::utils
