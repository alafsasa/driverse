#pragma once

#include "types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace driverse::core {

// How risky it is to touch a path (G4): credentials get the strongest
// warning, personal state a softer one.
enum class SensitiveLevel {
    None,          // nothing special about the path
    PersonalState, // user config/data trees — warn before acting
    Credential     // keys, credentials, wallet material — strong warning
};

class Protections {
public:
    static Protections& instance();
    
    // Check if a path is protected (system critical, never delete)
    bool isProtected(const std::filesystem::path& path) const;
    
    // Check if a path should be warned (sensitive but not hard-blocked)
    bool isSensitive(const std::filesystem::path& path) const;

    // Tiered variant of isSensitive(): None / PersonalState / Credential.
    SensitiveLevel sensitiveLevel(const std::filesystem::path& path) const;
    
    // Add custom protected path
    void addProtectedPath(const std::filesystem::path& path);
    
    // Remove protected path
    void removeProtectedPath(const std::filesystem::path& path);
    
    // Reset to defaults
    void resetToDefaults();
    
    // Get all protected paths
    const std::vector<std::filesystem::path>& getProtectedPaths() const;
    
private:
    Protections();
    ~Protections() = default;
    Protections(const Protections&) = delete;
    Protections& operator=(const Protections&) = delete;

    // Refresh the normalized fast-lookup cache after any mutation.
    void rebuildNormalizedCache();

    std::vector<std::filesystem::path> m_protectedPaths;
    // m_protectedPaths, absolute + lexically normalized (hot-path lookups).
    std::vector<std::string> m_protectedNormalized;
    std::vector<std::filesystem::path> m_sensitivePaths;
};

} // namespace driverse::core
