#include "utils/audit.h"

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
    return fs::temp_directory_path() / ("driverse_audit_" + std::to_string(::getpid())) /
           "audit.jsonl";
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

void testEscape() {
    assert(jsonEscape("plain") == "plain");
    assert(jsonEscape("a\"b") == "a\\\"b");
    assert(jsonEscape("a\\b") == "a\\\\b");
    assert(jsonEscape("line\nbreak") == "line\\nbreak");
    assert(jsonEscape("tab\there") == "tab\\there");
    // Control char -> \uXXXX
    assert(jsonEscape(std::string("x") + char(0x01)) == "x\\u0001");
    std::cout << "  jsonEscape OK\n";
}

void testToJsonShape() {
    AuditEntry e;
    e.timestamp = "2026-01-01T00:00:00Z";
    e.action = AuditAction::Cleanup;
    e.mode = "apply";
    e.user = "alice";
    e.detail = "cleanup batch";
    e.result = "partial";
    e.totalBytes = 4096;
    e.items.push_back({"/home/alice/.cache/thumbnails/a.png", 2048, "Safe", "trashed", ""});
    e.items.push_back({"/home/alice/.cache/thumbnails/b.png", 2048, "Safe", "failed", "EBUSY"});
    e.errors.push_back("EBUSY on b.png");

    const std::string json = AuditLog::toJson(e);
    assert(json.front() == '{' && json.back() == '}');
    assert(json.find("\"ts\":\"2026-01-01T00:00:00Z\"") != std::string::npos);
    assert(json.find("\"action\":\"cleanup\"") != std::string::npos);
    assert(json.find("\"mode\":\"apply\"") != std::string::npos);
    assert(json.find("\"total_bytes\":4096") != std::string::npos);
    assert(json.find("\"result\":\"partial\"") != std::string::npos);
    assert(json.find("\"error\":\"EBUSY\"") != std::string::npos);
    assert(json.find("\"errors\":[\"EBUSY on b.png\"]") != std::string::npos);
    assert(json.find('\n') == std::string::npos); // single line
    std::cout << "  toJson shape OK\n";
}

void testAppendAndRead() {
    const fs::path path = tempLogPath();
    fs::remove_all(path.parent_path());

    AuditLog log(path);
    assert(log.path() == path);

    AuditEntry e1;
    e1.action = AuditAction::Scan;
    e1.detail = "scan /home/alice";
    e1.result = "ok";
    assert(log.append(e1)); // creates parent dirs

    AuditEntry e2;
    e2.action = AuditAction::Cleanup;
    e2.mode = "dry-run";
    e2.items.push_back({"/tmp/x", 10, "Medium", "skipped", ""});
    e2.totalBytes = 10;
    assert(log.append(e2));

    auto lines = readLines(path);
    assert(lines.size() == 2);
    // Timestamps auto-filled.
    assert(lines[0].find("\"ts\":\"") != std::string::npos);
    assert(lines[0].find("\"action\":\"scan\"") != std::string::npos);
    assert(lines[1].find("\"mode\":\"dry-run\"") != std::string::npos);
    // Each line is a complete JSON object.
    for (const auto& l : lines) {
        assert(l.front() == '{');
        assert(l.back() == '}');
    }

    // Append again -> grows, never truncates.
    AuditEntry e3;
    e3.action = AuditAction::ConfigChange;
    assert(log.append(e3));
    assert(readLines(path).size() == 3);

    fs::remove_all(path.parent_path());
    std::cout << "  append/read OK\n";
}

void testEscapingInLog() {
    const fs::path path = tempLogPath();
    fs::remove_all(path.parent_path());

    AuditLog log(path);
    AuditEntry e;
    e.action = AuditAction::Cleanup;
    e.detail = "path with \"quotes\" and\nnewline";
    e.items.push_back({"/home/a b/\"weird\"", 1, "High", "deleted", ""});
    assert(log.append(e));

    auto lines = readLines(path);
    assert(lines.size() == 1);
    // The raw file must not contain a literal newline inside the record.
    assert(lines[0].find("\\n") != std::string::npos);
    assert(lines[0].find("\\\"") != std::string::npos);

    fs::remove_all(path.parent_path());
    std::cout << "  escaping in log OK\n";
}

void testDefaultPath() {
    ::setenv("XDG_DATA_HOME", "/tmp/xdgdata", 1);
    assert(AuditLog::defaultPath() == fs::path("/tmp/xdgdata/driverse/audit.jsonl"));
    ::unsetenv("XDG_DATA_HOME");
    std::cout << "  defaultPath XDG OK\n";
}

} // namespace

int main() {
    std::cout << "Running audit tests...\n";
    testEscape();
    testToJsonShape();
    testAppendAndRead();
    testEscapingInLog();
    testDefaultPath();
    std::cout << "All audit tests passed!\n";
    return 0;
}
