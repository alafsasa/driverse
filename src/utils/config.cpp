#include "config.h"

#include "core/utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace driverse::utils {

namespace {

std::string trim(const std::string& s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(s.begin(), s.end(), notSpace);
    auto end = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    if (begin >= end) return {};
    return std::string(begin, end);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string sectionOf(const std::string& key) {
    const auto pos = key.find('.');
    return pos == std::string::npos ? "general" : key.substr(0, pos);
}

std::string nameOf(const std::string& key) {
    const auto pos = key.find('.');
    return pos == std::string::npos ? key : key.substr(pos + 1);
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string part;
    while (std::getline(ss, part, ',')) {
        std::string t = trim(part);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

std::string joinList(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ", ";
        out += values[i];
    }
    return out;
}

} // namespace

Config Config::defaults() {
    Config c;

    c.setInt("general.min_age_safe_days", 7);
    c.setInt("general.min_age_temp_days", 3);
    c.setInt("general.min_age_downloads_days", 90);
    c.setBool("general.allow_permanent_delete", true);

    std::vector<std::string> protectedPaths;
    for (const auto& p : core::utils::getDefaultProtectedPaths()) {
        protectedPaths.push_back(p.string());
    }
    c.setStringList("protected.paths", protectedPaths);

    std::vector<std::string> excludedPaths;
    for (const auto& p : core::utils::getDefaultExcludedPaths()) {
        excludedPaths.push_back(p.string());
    }
    c.setStringList("exclude.paths", excludedPaths);
    c.setStringList("exclude.fs_types", core::utils::getDefaultExcludedFsTypes());

    return c;
}

Config Config::load(const std::filesystem::path& path) {
    Config c = defaults();

    std::ifstream in(path);
    if (!in.is_open()) {
        return c; // missing file -> defaults
    }

    std::string section = "general";
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos) {
            continue; // ignore malformed lines
        }
        const std::string name = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));
        if (name.empty()) continue;
        c.m_values[section + "." + name] = value;
    }
    return c;
}

bool Config::save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    std::string currentSection;
    for (const auto& [key, value] : m_values) {
        const std::string section = sectionOf(key);
        if (section != currentSection) {
            if (!currentSection.empty()) out << '\n';
            out << '[' << section << "]\n";
            currentSection = section;
        }
        out << nameOf(key) << " = " << value << '\n';
    }
    out.flush();
    return out.good();
}

bool Config::has(const std::string& key) const { return m_values.count(key) > 0; }

void Config::remove(const std::string& key) { m_values.erase(key); }

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? defaultValue : it->second;
}

std::int64_t Config::getInt(const std::string& key, std::int64_t defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return defaultValue;
    try {
        return std::stoll(trim(it->second));
    } catch (...) {
        return defaultValue;
    }
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end()) return defaultValue;
    const std::string v = toLower(trim(it->second));
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return defaultValue;
}

std::vector<std::string> Config::getStringList(const std::string& key) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? std::vector<std::string>{} : splitList(it->second);
}

void Config::setString(const std::string& key, const std::string& value) {
    m_values[key] = value;
}

void Config::setInt(const std::string& key, std::int64_t value) {
    m_values[key] = std::to_string(value);
}

void Config::setBool(const std::string& key, bool value) {
    m_values[key] = value ? "true" : "false";
}

void Config::setStringList(const std::string& key, const std::vector<std::string>& values) {
    m_values[key] = joinList(values);
}

std::vector<std::filesystem::path> Config::protectedPaths() const {
    std::vector<std::filesystem::path> out;
    for (const auto& s : getStringList("protected.paths")) {
        out.emplace_back(s);
    }
    return out;
}

std::vector<std::filesystem::path> Config::excludedPaths() const {
    std::vector<std::filesystem::path> out;
    for (const auto& s : getStringList("exclude.paths")) {
        out.emplace_back(s);
    }
    return out;
}

std::vector<std::string> Config::excludedFilesystemTypes() const {
    return getStringList("exclude.fs_types");
}

std::filesystem::path defaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = home && *home ? std::filesystem::path(home) / ".config" : std::filesystem::path(".");
    }
    return base / "driverse" / "driverse.conf";
}

} // namespace driverse::utils
