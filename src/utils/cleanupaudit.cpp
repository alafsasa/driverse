#include "cleanupaudit.h"

#include "core/utils.h"

#include <unordered_map>
#include <utility>

namespace driverse::utils {

namespace {

std::string riskName(core::RiskLevel risk) {
    return core::utils::riskLevelToString(risk);
}

std::unordered_map<std::string, core::RiskLevel> riskIndex(const core::CleanupPlan& plan) {
    std::unordered_map<std::string, core::RiskLevel> index;
    index.reserve(plan.items.size());
    for (const auto& item : plan.items) {
        index.emplace(item.path.string(), item.risk);
    }
    return index;
}

std::string overallResult(const core::CleanupReport& report) {
    if (report.aborted) {
        return "failed";
    }
    if (report.failed > 0) {
        const bool someSucceeded = (report.moved + report.deleted + report.dryRun) > 0;
        return someSucceeded ? "partial" : "failed";
    }
    return "ok";
}

} // namespace

AuditEntry makeCleanupBeginEntry(const core::CleanupPlan& plan, const std::string& user) {
    AuditEntry entry;
    entry.action = AuditAction::Cleanup;
    entry.mode = "apply";
    entry.user = user;
    entry.detail = "cleanup begin";
    entry.totalBytes = plan.totalBytes;

    entry.items.reserve(plan.items.size());
    for (const auto& item : plan.items) {
        if (item.action == core::CleanupAction::Skip) {
            continue;
        }
        AuditItem auditItem;
        auditItem.path = item.path.string();
        auditItem.bytes = item.allocatedSize;
        auditItem.risk = riskName(item.risk);
        auditItem.result = "planned";
        entry.items.push_back(std::move(auditItem));
    }
    return entry;
}

AuditEntry makeCleanupResultEntry(const core::CleanupPlan& plan,
                                  const core::CleanupReport& report,
                                  const std::string& user) {
    const bool dryRun = report.mode == core::CleanupMode::DryRun;

    AuditEntry entry;
    entry.action = dryRun ? AuditAction::Preview : AuditAction::Cleanup;
    entry.mode = dryRun ? "dry-run" : "apply";
    entry.user = user;
    entry.detail = dryRun ? "dry-run preview" : "cleanup complete";
    entry.totalBytes = dryRun ? report.bytesPlanned
                              : (report.bytesFreed + report.bytesTrashed);
    entry.result = overallResult(report);

    const auto riskByPath = riskIndex(plan);
    entry.items.reserve(report.items.size());
    for (const auto& item : report.items) {
        AuditItem auditItem;
        auditItem.path = item.path.string();
        auditItem.bytes = item.bytes;
        auditItem.result = core::cleanupStatusToString(item.status);
        auditItem.error = item.status == core::CleanupStatus::Failed ? item.detail : "";
        const auto it = riskByPath.find(auditItem.path);
        if (it != riskByPath.end()) {
            auditItem.risk = riskName(it->second);
        }
        entry.items.push_back(std::move(auditItem));

        if (item.status == core::CleanupStatus::Failed) {
            entry.errors.push_back(auditItem.path + ": " + item.detail);
        }
    }

    if (report.aborted && !report.abortReason.empty()) {
        entry.errors.push_back("aborted: " + report.abortReason);
    }
    return entry;
}

} // namespace driverse::utils
