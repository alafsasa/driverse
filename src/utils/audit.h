#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace driverse::utils {

enum class AuditAction {
    Scan,
    Preview,   // dry-run
    Cleanup,   // apply
    ConfigChange,
    Other
};

std::string toString(AuditAction action);

// One affected path within an entry.
struct AuditItem {
    std::string path;
    std::uintmax_t bytes = 0;
    std::string risk;    // Safe / Low / Medium / High / System Protected
    std::string result;  // deleted / trashed / skipped / failed
    std::string error;   // empty on success
};

// A single, self-contained audit record. One JSON object per line on disk.
struct AuditEntry {
    std::string timestamp;   // ISO 8601 (UTC); auto-filled by append() if empty
    AuditAction action = AuditAction::Other;
    std::string mode;        // "dry-run" | "apply" | ""
    std::string user;        // username / uid string
    std::string detail;      // free-form context (e.g. scan root, config key)
    std::vector<AuditItem> items;
    std::uintmax_t totalBytes = 0; // sum of recoverable/affected bytes
    std::string result;      // ok | partial | failed | ""
    std::vector<std::string> errors;
};

// Append-only JSON Lines audit log. Each append writes one complete line and
// flushes, so a crash cannot corrupt earlier records.
class AuditLog {
public:
    explicit AuditLog(std::filesystem::path path);

    // Serialize `entry` (filling timestamp if empty) and append one JSON line.
    // Creates parent directories as needed. Returns false on I/O error.
    bool append(AuditEntry entry);

    const std::filesystem::path& path() const { return m_path; }

    // Serialize a single entry to one-line JSON (no trailing newline).
    static std::string toJson(const AuditEntry& entry);

    // ISO 8601 UTC timestamp, e.g. "2026-10-15T12:34:56Z".
    static std::string nowIso8601();

    // Default log path:
    //   $XDG_DATA_HOME/driverse/audit.jsonl
    //   else $HOME/.local/share/driverse/audit.jsonl
    static std::filesystem::path defaultPath();

private:
    std::filesystem::path m_path;
};

// Escape a string for inclusion in a JSON document.
std::string jsonEscape(const std::string& s);

} // namespace driverse::utils
