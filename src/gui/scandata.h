#pragma once

#include "core/types.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace driverse::gui {

// Immutable snapshot of one completed scan, shared between the worker and the
// results models. Held by std::shared_ptr so a new scan can replace it while the
// views still reference the previous one.
struct ScanData {
    QString root;
    core::ScanResult result;
    std::vector<core::CleanupCandidate> recommendations;
    // Fast lookup: absolute path -> recommended risk (for the Files/Dirs tabs).
    std::unordered_map<std::string, core::RiskLevel> riskByPath;
    std::uintmax_t totalRecoverable = 0;
};

using ScanDataPtr = std::shared_ptr<const ScanData>;

} // namespace driverse::gui
