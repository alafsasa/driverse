#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace driverse::utils {

// Simple, dependency-free INI-style configuration store.
//
// Format (sections + key = value; '#' or ';' start comments):
//
//   [general]
//   min_age_safe_days = 7
//   allow_permanent_delete = true
//   [protected]
//   paths = /boot, /efi, /usr, /etc
//
// Lists are stored as comma-separated values on a single line.
//
// Keys are addressed as "section.name" (e.g. "general.min_age_safe_days").
class Config {
public:
    // Load from `path` if it exists; missing keys fall back to built-in
    // defaults. Never throws; parse errors are ignored line by line.
    static Config load(const std::filesystem::path& path);

    // Built-in defaults (no file I/O).
    static Config defaults();

    // Write to `path`, creating parent directories. Returns false on I/O error.
    bool save(const std::filesystem::path& path) const;

    bool has(const std::string& key) const;
    void remove(const std::string& key);

    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    std::int64_t getInt(const std::string& key, std::int64_t defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    std::vector<std::string> getStringList(const std::string& key) const;

    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, std::int64_t value);
    void setBool(const std::string& key, bool value);
    void setStringList(const std::string& key, const std::vector<std::string>& values);

    // All entries (flattened "section.key" -> raw value), for inspection/UI.
    const std::map<std::string, std::string>& entries() const { return m_values; }

    // Typed, well-known accessors with defaults applied.
    std::vector<std::filesystem::path> protectedPaths() const;
    std::vector<std::filesystem::path> excludedPaths() const;
    std::vector<std::string> excludedFilesystemTypes() const;

private:
    std::map<std::string, std::string> m_values;
};

// Path of the user configuration file:
//   $XDG_CONFIG_HOME/driverse/driverse.conf
//   else $HOME/.config/driverse/driverse.conf
std::filesystem::path defaultConfigPath();

} // namespace driverse::utils
