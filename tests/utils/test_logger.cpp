#include "utils/logger.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

using namespace driverse::utils;
namespace fs = std::filesystem;

namespace {

fs::path tempLogPath() {
    return fs::temp_directory_path() / ("driverse_logger_" + std::to_string(::getpid())) /
           "app.log";
}

std::vector<std::string> readLines(const fs::path& p) {
    std::vector<std::string> lines;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

void testLevelFiltering() {
    Logger logger;
    logger.setConsole(false);
    assert(logger.minLevel() == LogLevel::Info);

    const fs::path path = tempLogPath();
    fs::remove_all(path.parent_path());
    assert(logger.setLogFile(path));

    logger.setMinLevel(LogLevel::Info);
    logger.debug("dropped");
    logger.info("kept-info");
    logger.warn("kept-warn");
    logger.error("kept-error");

    auto lines = readLines(path);
    assert(lines.size() == 3); // debug dropped
    assert(lines[0].find("kept-info") != std::string::npos);
    assert(lines[1].find("kept-warn") != std::string::npos);
    assert(lines[2].find("kept-error") != std::string::npos);
    // Level tags present.
    assert(lines[0].find("[INFO]") != std::string::npos);
    assert(lines[1].find("[WARN]") != std::string::npos);
    assert(lines[2].find("[ERROR]") != std::string::npos);

    // Raising the level to Error drops info+warn.
    fs::remove_all(path);
    logger.setMinLevel(LogLevel::Error);
    logger.info("dropped");
    logger.warn("dropped");
    logger.error("kept");
    assert(readLines(path).size() == 1);

    fs::remove_all(path.parent_path());
    std::cout << "  level filtering OK\n";
}

void testFileAppend() {
    Logger logger;
    logger.setConsole(false);
    const fs::path path = tempLogPath();
    fs::remove_all(path.parent_path());
    logger.setLogFile(path);

    logger.info("one");
    logger.info("two");
    logger.info("three");
    auto lines = readLines(path);
    assert(lines.size() == 3); // appended, not truncated
    assert(lines[0].find("one") != std::string::npos);
    assert(lines[2].find("three") != std::string::npos);

    fs::remove_all(path.parent_path());
    std::cout << "  file append OK\n";
}

void testNoFileSink() {
    Logger logger;
    logger.setConsole(false);
    // No file configured: log() must not crash.
    logger.info("nowhere");
    logger.error("nowhere");
    std::cout << "  no-file sink OK\n";
}

void testLevelNames() {
    assert(toString(LogLevel::Debug) == "DEBUG");
    assert(toString(LogLevel::Info) == "INFO");
    assert(toString(LogLevel::Warn) == "WARN");
    assert(toString(LogLevel::Error) == "ERROR");
    std::cout << "  level names OK\n";
}

} // namespace

int main() {
    std::cout << "Running logger tests...\n";
    testLevelNames();
    testLevelFiltering();
    testFileAppend();
    testNoFileSink();
    std::cout << "All logger tests passed!\n";
    return 0;
}
