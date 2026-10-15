#include "audit.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include <unistd.h>

namespace driverse::utils {

namespace {

std::string currentUserName() {
    const char* u = std::getenv("USER");
    if (u && *u) return u;
    const char* logname = std::getenv("LOGNAME");
    if (logname && *logname) return logname;
    return std::to_string(::getuid());
}

void appendUint(std::ostringstream& os, std::uintmax_t value) {
    os << value; // uintmax_t prints exactly; safe for JSON numbers
}

} // namespace

std::string toString(AuditAction action) {
    switch (action) {
        case AuditAction::Scan: return "scan";
        case AuditAction::Preview: return "preview";
        case AuditAction::Cleanup: return "cleanup";
        case AuditAction::ConfigChange: return "config_change";
        case AuditAction::Other: return "other";
    }
    return "other";
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string AuditLog::nowIso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string AuditLog::toJson(const AuditEntry& entry) {
    std::ostringstream os;
    os << '{';
    os << "\"ts\":\"" << jsonEscape(entry.timestamp) << '"';
    os << ",\"action\":\"" << jsonEscape(toString(entry.action)) << '"';
    if (!entry.mode.empty()) {
        os << ",\"mode\":\"" << jsonEscape(entry.mode) << '"';
    }
    os << ",\"user\":\"" << jsonEscape(entry.user) << '"';
    if (!entry.detail.empty()) {
        os << ",\"detail\":\"" << jsonEscape(entry.detail) << '"';
    }
    if (!entry.result.empty()) {
        os << ",\"result\":\"" << jsonEscape(entry.result) << '"';
    }
    os << ",\"total_bytes\":";
    appendUint(os, entry.totalBytes);

    os << ",\"items\":[";
    for (std::size_t i = 0; i < entry.items.size(); ++i) {
        const auto& item = entry.items[i];
        if (i) os << ',';
        os << "{\"path\":\"" << jsonEscape(item.path) << '"';
        if (!item.risk.empty()) os << ",\"risk\":\"" << jsonEscape(item.risk) << '"';
        if (!item.result.empty()) os << ",\"result\":\"" << jsonEscape(item.result) << '"';
        os << ",\"bytes\":";
        appendUint(os, item.bytes);
        if (!item.error.empty()) os << ",\"error\":\"" << jsonEscape(item.error) << '"';
        os << '}';
    }
    os << ']';

    if (!entry.errors.empty()) {
        os << ",\"errors\":[";
        for (std::size_t i = 0; i < entry.errors.size(); ++i) {
            if (i) os << ',';
            os << '"' << jsonEscape(entry.errors[i]) << '"';
        }
        os << ']';
    }
    os << '}';
    return os.str();
}

AuditLog::AuditLog(std::filesystem::path path) : m_path(std::move(path)) {}

bool AuditLog::append(AuditEntry entry) {
    if (entry.timestamp.empty()) {
        entry.timestamp = nowIso8601();
    }
    if (entry.user.empty()) {
        entry.user = currentUserName();
    }

    std::error_code ec;
    if (m_path.has_parent_path()) {
        std::filesystem::create_directories(m_path.parent_path(), ec);
    }

    std::ofstream out(m_path, std::ios::app);
    if (!out.is_open()) {
        return false;
    }
    out << toJson(entry) << '\n';
    out.flush();
    return out.good();
}

std::filesystem::path AuditLog::defaultPath() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = home && *home ? std::filesystem::path(home) / ".local" / "share"
                             : std::filesystem::path(".");
    }
    return base / "driverse" / "audit.jsonl";
}

} // namespace driverse::utils
