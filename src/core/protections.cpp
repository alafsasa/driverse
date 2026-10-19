#include "protections.h"
#include "utils.h"
#include <algorithm>
#include <string_view>

namespace driverse::core {

Protections& Protections::instance() {
    static Protections inst;
    return inst;
}

Protections::Protections() {
    resetToDefaults();
}

bool Protections::isProtected(const std::filesystem::path& path) const {
    // Built-in defaults...
    if (utils::isProtectedPath(path)) {
        return true;
    }
    // ...plus any paths registered at runtime (e.g. from user config).
    if (m_protectedNormalized.empty()) {
        return false;
    }
    const std::string abs = utils::normalizedAbsoluteString(path);
    for (const auto& prot : m_protectedNormalized) {
        if (utils::isPathUnderNormalized(abs, prot)) {
            return true;
        }
    }
    return false;
}

bool Protections::isSensitive(const std::filesystem::path& path) const {
    return sensitiveLevel(path) != SensitiveLevel::None;
}

namespace {

using Sv = std::string_view;

// Split an absolute path into non-empty components (purely lexical).
std::vector<Sv> splitComponents(const std::string& p) {
    std::vector<Sv> out;
    std::size_t pos = 0;
    while (pos < p.size()) {
        while (pos < p.size() && p[pos] == '/') ++pos;
        if (pos >= p.size()) break;
        std::size_t end = p.find('/', pos);
        if (end == std::string::npos) end = p.size();
        out.emplace_back(p.data() + pos, end - pos);
        pos = end;
    }
    return out;
}

template <std::size_t N>
bool anyEqual(Sv comp, const Sv (&list)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (comp == list[i]) return true;
    }
    return false;
}

bool hasSuffix(Sv s, Sv suffix) {
    return s.size() > suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Directory names that hold keys/credentials wherever they appear.
constexpr Sv kCredentialDirs[] = {
    ".ssh", ".gnupg", ".aws", ".kube", ".docker", ".password-store",
    ".gnome-keyring", "keyrings", ".pki",
};

// File names that are secrets by themselves.
constexpr Sv kCredentialFiles[] = {
    "id_rsa", "id_dsa", "id_ecdsa", "id_ed25519",
    ".netrc", ".git-credentials", ".env",
    "credentials", "credentials.json", "secrets.json",
    "wallet.dat", "wallet.db", "keyring", "keystore",
};

// Extensions of private-key / keystore material.
constexpr Sv kCredentialExts[] = {
    ".pem", ".key", ".pfx", ".p12", ".jks", ".kdbx",
};

// Well-known cache directory names: their contents are regenerable by
// definition, so they are exempt from the personal-state warning (browser
// profile caches, Chromium/Code caches, thumbnail stores, ...).
constexpr Sv kCacheDirs[] = {
    ".cache", "cache", "Cache", "cache2", "Caches", "CachedData",
    "GPUCache", "ShaderCache", "Code Cache", "thumbnails",
};

// Personal-state trees the user would not want silently swept away: config,
// app data/state, browser & mail profiles, Flatpak per-app state, and the
// shared data home of a snap.
bool inPersonalStateTree(const std::vector<Sv>& comps) {
    for (std::size_t i = 0; i < comps.size(); ++i) {
        if (comps[i] == ".config" || comps[i] == ".mozilla" ||
            comps[i] == ".thunderbird") {
            return true;
        }
        if (comps[i] == ".local" && i + 1 < comps.size() &&
            (comps[i + 1] == "share" || comps[i + 1] == "state")) {
            return true;
        }
        // ~/.var/app/<app-id>/{data,config}
        if (comps[i] == ".var" && i + 3 < comps.size() && comps[i + 1] == "app" &&
            (comps[i + 3] == "data" || comps[i + 3] == "config")) {
            return true;
        }
        // ~/snap/<name>/common
        if (comps[i] == "snap" && i + 2 < comps.size() && comps[i + 2] == "common") {
            return true;
        }
    }
    return false;
}

bool hasCacheComponent(const std::vector<Sv>& comps) {
    for (const Sv c : comps) {
        if (anyEqual(c, kCacheDirs)) return true;
    }
    return false;
}

} // namespace

SensitiveLevel Protections::sensitiveLevel(const std::filesystem::path& path) const {
    const std::string abs = utils::normalizedAbsoluteString(path);
    const std::vector<Sv> comps = splitComponents(abs);
    if (comps.empty()) {
        return SensitiveLevel::None;
    }

    // 1) Credential material anywhere in the path — strongest warning, and it
    // deliberately wins over the cache exemption below.
    for (const Sv c : comps) {
        if (anyEqual(c, kCredentialDirs)) {
            return SensitiveLevel::Credential;
        }
    }
    const Sv last = comps.back();
    if (anyEqual(last, kCredentialFiles)) {
        return SensitiveLevel::Credential;
    }
    for (const Sv ext : kCredentialExts) {
        if (hasSuffix(last, ext)) {
            return SensitiveLevel::Credential;
        }
    }

    // 2) Personal state trees — warn, unless the path sits inside an explicit
    // cache directory (browser/Chromium caches and friends are regenerable).
    if (inPersonalStateTree(comps) && !hasCacheComponent(comps)) {
        return SensitiveLevel::PersonalState;
    }
    return SensitiveLevel::None;
}

void Protections::addProtectedPath(const std::filesystem::path& path) {
    m_protectedPaths.push_back(path);
    rebuildNormalizedCache();
}

void Protections::removeProtectedPath(const std::filesystem::path& path) {
    auto it = std::find(m_protectedPaths.begin(), m_protectedPaths.end(), path);
    if (it != m_protectedPaths.end()) {
        m_protectedPaths.erase(it);
        rebuildNormalizedCache();
    }
}

void Protections::resetToDefaults() {
    m_protectedPaths = utils::getDefaultProtectedPaths();
    rebuildNormalizedCache();
}

void Protections::rebuildNormalizedCache() {
    m_protectedNormalized.clear();
    m_protectedNormalized.reserve(m_protectedPaths.size());
    for (const auto& p : m_protectedPaths) {
        // Normalized form (trailing slashes stripped, "."/".." resolved) —
        // isPathUnderNormalized() is a boundary-exact prefix test and would
        // otherwise miss "/boot/" style config entries.
        m_protectedNormalized.push_back(utils::normalizedAbsoluteString(p));
    }
}

const std::vector<std::filesystem::path>& Protections::getProtectedPaths() const {
    return m_protectedPaths;
}

} // namespace driverse::core
