#pragma once

#include "types.h"
#include <filesystem>
#include <vector>

namespace driverse::core {

class Protections {
public:
    static Protections& instance();
    
    // Check if a path is protected (system critical, never delete)
    bool isProtected(const std::filesystem::path& path) const;
    
    // Check if a path should be warned (sensitive but not hard-blocked)
    bool isSensitive(const std::filesystem::path& path) const;
    
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
    
    std::vector<std::filesystem::path> m_protectedPaths;
    std::vector<std::filesystem::path> m_sensitivePaths;
};

} // namespace driverse::core
