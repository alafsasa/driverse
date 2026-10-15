#include "logger.h"

#include <ctime>
#include <fstream>
#include <iostream>

namespace driverse::utils {

std::string toString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::minLevel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_minLevel;
}

bool Logger::setLogFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logFile = path;
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    // Probe that we can open it.
    std::ofstream probe(path, std::ios::app);
    return probe.is_open();
}

const std::filesystem::path& Logger::logFile() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_logFile;
}

void Logger::setConsole(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_console = enabled;
}

std::string Logger::timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(level) < static_cast<int>(m_minLevel)) {
        return;
    }

    const std::string line = timestamp() + " [" + toString(level) + "] " + message;

    if (m_console) {
        if (level == LogLevel::Warn || level == LogLevel::Error) {
            std::cerr << line << '\n';
        } else {
            std::cout << line << '\n';
        }
    }

    if (!m_logFile.empty()) {
        std::ofstream out(m_logFile, std::ios::app);
        if (out.is_open()) {
            out << line << '\n';
        }
    }
}

} // namespace driverse::utils
