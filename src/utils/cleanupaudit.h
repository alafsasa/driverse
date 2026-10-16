#pragma once

#include "audit.h"
#include "core/cleanup.h"

#include <string>

namespace driverse::utils {

// Audit entries for the cleanup workflow. The plan/report remain plain core
// types; this is the only place that knows how they map onto the JSONL log.

// Logged immediately before an apply starts: every item that will be touched,
// with its risk, as "planned".
AuditEntry makeCleanupBeginEntry(const core::CleanupPlan& plan, const std::string& user);

// Logged after an execution.
//   dry-run -> action=Preview, mode="dry-run"
//   apply   -> action=Cleanup, mode="apply", result=ok|partial|failed
AuditEntry makeCleanupResultEntry(const core::CleanupPlan& plan,
                                  const core::CleanupReport& report,
                                  const std::string& user);

} // namespace driverse::utils
