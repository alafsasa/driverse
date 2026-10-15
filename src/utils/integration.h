#pragma once

#include "config.h"
#include "core/protections.h"
#include "core/recommendations.h"
#include "core/scanner.h"

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace driverse::utils {

// Translation layer between the user-facing Config and the core engine types.
// This keeps Config free of core dependencies and gives Phases 8-10 a single
// place to obtain consistently-configured options.

// Build recommendation-engine options from config.
// `home` is the user's home directory (used for relative cache matching and
// ownership). `now` overrides the age reference clock (defaults to wall clock).
core::RecommendationOptions recommendationOptionsFromConfig(
    const Config& config,
    const std::filesystem::path& home,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

// Build scanner options for `root`, applying configured excluded paths.
core::ScanOptions scanOptionsFromConfig(const Config& config,
                                        const std::filesystem::path& root);

// Register every path in config "protected.paths" with the global protections
// registry. Returns the number of paths newly added (idempotent).
std::size_t applyProtectedPaths(const Config& config, core::Protections& protections);

} // namespace driverse::utils
