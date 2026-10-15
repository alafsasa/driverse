#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <filesystem>

namespace driverse::core::utils {

// Format bytes to human readable string
std::string formatBytes(std::uintmax_t bytes);

// Format duration in milliseconds
std::string formatDuration(std::chrono::milliseconds ms);

// Check if path is under a protected root
bool isProtectedPath(const std::filesystem::path& path);

// Get default protected paths
std::vector<std::filesystem::path> getDefaultProtectedPaths();

// Get default excluded paths/patterns (pseudo-filesystems)
std::vector<std::filesystem::path> getDefaultExcludedPaths();

// Get default excluded filesystem types
std::vector<std::string> getDefaultExcludedFsTypes();

// Normalize path (resolve .. where safe, but don't follow symlinks)
std::filesystem::path normalizePath(const std::filesystem::path& path);

// Convert skip reason to string
std::string skipReasonToString(SkipReason reason);

// Convert risk level to string
std::string riskLevelToString(RiskLevel risk);

// Convert confidence level to string
std::string confidenceLevelToString(ConfidenceLevel confidence);

// Check if filesystem is pseudo
bool isPseudoFilesystem(const std::string& fsType);

// Check if filesystem is network
bool isNetworkFilesystem(const std::string& fsType);

} // namespace driverse::core::utils
