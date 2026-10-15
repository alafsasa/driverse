#include "protections.h"
#include "utils.h"
#include <algorithm>

namespace driverse::core {

Protections& Protections::instance() {
    static Protections inst;
    return inst;
}

Protections::Protections() {
    resetToDefaults();
}

bool Protections::isProtected(const std::filesystem::path& path) const {
    return utils::isProtectedPath(path);
}

bool Protections::isSensitive(const std::filesystem::path& path) const {
    // Sensitive paths that warn but might be user-controlled
    auto absPath = std::filesystem::absolute(path);
    std::string absStr = absPath.string();
    
    // User config dirs - generally not safe to blindly clean
    if (absStr.find("/.config/") != std::string::npos ||
        absStr.find("/.local/") != std::string::npos ||
        absStr.find("/.gnupg/") != std::string::npos ||
        absStr.find("/.ssh/") != std::string::npos) {
        return true;
    }
    
    return false;
}

void Protections::addProtectedPath(const std::filesystem::path& path) {
    m_protectedPaths.push_back(path);
}

void Protections::removeProtectedPath(const std::filesystem::path& path) {
    auto it = std::find(m_protectedPaths.begin(), m_protectedPaths.end(), path);
    if (it != m_protectedPaths.end()) {
        m_protectedPaths.erase(it);
    }
}

void Protections::resetToDefaults() {
    m_protectedPaths = utils::getDefaultProtectedPaths();
}

const std::vector<std::filesystem::path>& Protections::getProtectedPaths() const {
    return m_protectedPaths;
}

} // namespace driverse::core
