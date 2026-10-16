#include "cleanupdialog.h"

#include "core/utils.h"
#include "formatting.h"
#include "utils/audit.h"
#include "utils/cleanupaudit.h"

#include <unistd.h>

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace driverse::gui {

namespace {

constexpr int kMaxTableRows = 500;
const char* const kTypeWord = "DELETE";

QString actionName(core::CleanupAction action) {
    switch (action) {
        case core::CleanupAction::MoveToTrash:     return QObject::tr("Move to trash");
        case core::CleanupAction::PermanentDelete: return QObject::tr("Permanently delete");
        case core::CleanupAction::Skip:            return QObject::tr("Skip");
    }
    return QString();
}

QString currentUser() {
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) {
        user = QString::number(static_cast<qulonglong>(::getuid()));
    }
    return user;
}

} // namespace

// ---- Confirmation gates ----------------------------------------------------

bool planHasActionableRisk(const core::CleanupPlan& plan, core::RiskLevel risk) {
    return std::any_of(plan.items.begin(), plan.items.end(),
                       [risk](const core::CleanupItem& item) {
                           return item.action != core::CleanupAction::Skip && item.risk == risk;
                       });
}

bool permanentDeleteOffered(const core::CleanupPlan& plan) {
    return std::any_of(plan.items.begin(), plan.items.end(),
                       [](const core::CleanupItem& item) {
                           return item.action != core::CleanupAction::Skip &&
                                  item.risk == core::RiskLevel::Safe &&
                                  item.permanentDeleteAllowed;
                       });
}

bool typeConfirmationRequired(const core::CleanupPlan& plan,
                             const CleanupConfirmation& confirmation) {
    return confirmation.permanentDelete || planHasActionableRisk(plan, core::RiskLevel::High);
}

bool applyEnabled(const core::CleanupPlan& plan, const CleanupConfirmation& confirmation) {
    if (plan.actionableCount == 0) {
        return false;
    }
    if (!confirmation.actionAcknowledged) {
        return false;
    }
    if (planHasActionableRisk(plan, core::RiskLevel::High) && !confirmation.highRiskReviewed) {
        return false;
    }
    if (typeConfirmationRequired(plan, confirmation) &&
        confirmation.typedConfirmation != QLatin1String(kTypeWord)) {
        return false;
    }
    return true;
}

// ---- Dialog ----------------------------------------------------------------

CleanupDialog::CleanupDialog(std::vector<core::CleanupCandidate> candidates, QWidget* parent)
    : QDialog(parent), m_candidates(std::move(candidates)) {
    setWindowTitle(tr("Cleanup preview"));
    setModal(true);
    resize(880, 680);

    m_options.mode = core::CleanupMode::DryRun;
    m_options.homeDirectory = std::filesystem::path(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString());

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Nothing is modified until you press <b>Apply</b>. Files are first moved to the "
           "trash so they stay recoverable; permanent deletion is opt-in and limited to "
           "regenerable caches flagged Safe."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // ---- Plan summary ----
    auto* planBox = new QGroupBox(tr("Plan"), this);
    auto* planLayout = new QVBoxLayout(planBox);
    m_summaryLabel = new QLabel(planBox);
    m_summaryLabel->setWordWrap(true);
    m_breakdownLabel = new QLabel(planBox);
    m_breakdownLabel->setWordWrap(true);
    m_actionLabel = new QLabel(planBox);
    m_actionLabel->setWordWrap(true);
    planLayout->addWidget(m_summaryLabel);
    planLayout->addWidget(m_breakdownLabel);
    planLayout->addWidget(m_actionLabel);
    layout->addWidget(planBox);

    // ---- Item table ----
    m_items = new QTableWidget(this);
    m_items->setColumnCount(4);
    m_items->setHorizontalHeaderLabels({tr("Item"), tr("Risk"), tr("Size"),
                                        tr("Planned action")});
    m_items->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_items->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_items->setSelectionMode(QAbstractItemView::SingleSelection);
    m_items->setAlternatingRowColors(true);
    m_items->verticalHeader()->setVisible(false);
    m_items->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_items->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_items->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_items->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_items, 1);

    m_tableNote = new QLabel(this);
    m_tableNote->setWordWrap(true);
    layout->addWidget(m_tableNote);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setObjectName(QStringLiteral("warningLabel"));
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    // ---- Confirmation ----
    auto* confirmBox = new QGroupBox(tr("Confirmation"), this);
    auto* confirmLayout = new QVBoxLayout(confirmBox);

    m_confirmAction = new QCheckBox(confirmBox);
    m_confirmAction->setObjectName(QStringLiteral("confirmAction"));
    m_confirmHigh = new QCheckBox(
        tr("I have reviewed the high risk items above and accept that they may contain "
           "user data."),
        confirmBox);
    m_confirmHigh->setObjectName(QStringLiteral("confirmHigh"));
    m_confirmPermanent = new QCheckBox(
        tr("Permanently delete eligible Safe caches instead of trashing them "
           "(NOT recoverable)."),
        confirmBox);
    m_confirmPermanent->setObjectName(QStringLiteral("confirmPermanent"));
    confirmLayout->addWidget(m_confirmAction);
    confirmLayout->addWidget(m_confirmHigh);
    confirmLayout->addWidget(m_confirmPermanent);

    m_typeRow = new QWidget(confirmBox);
    m_typeRow->setObjectName(QStringLiteral("typeRow"));
    auto* typeLayout = new QHBoxLayout(m_typeRow);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    auto* typeLabel = new QLabel(tr("Type <b>%1</b> to confirm:").arg(QLatin1String(kTypeWord)),
                                 m_typeRow);
    m_typeConfirm = new QLineEdit(m_typeRow);
    m_typeConfirm->setObjectName(QStringLiteral("typeConfirm"));
    m_typeConfirm->setPlaceholderText(QLatin1String(kTypeWord));
    m_typeConfirm->setMaximumWidth(160);
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeConfirm);
    typeLayout->addStretch(1);
    confirmLayout->addWidget(m_typeRow);
    layout->addWidget(confirmBox);

    // ---- Report ----
    m_report = new QTextBrowser(this);
    m_report->setMinimumHeight(96);
    m_report->setMaximumHeight(180);
    m_report->setHtml(tr("<p style='color:#888'>No cleanup has been run yet.</p>"));
    layout->addWidget(m_report);

    // ---- Buttons ----
    auto* buttonRow = new QHBoxLayout();
    m_dryRunButton = new QPushButton(tr("Dry run"), this);
    m_dryRunButton->setObjectName(QStringLiteral("dryRunButton"));
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setObjectName(QStringLiteral("applyButton"));
    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    m_applyButton->setDefault(true);
    buttonRow->addWidget(m_dryRunButton);
    buttonRow->addWidget(m_applyButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    connect(m_dryRunButton, &QPushButton::clicked, this, &CleanupDialog::runDryRun);
    connect(m_applyButton, &QPushButton::clicked, this, &CleanupDialog::runApply);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_confirmAction, &QCheckBox::toggled, this, &CleanupDialog::updateConfirmationState);
    connect(m_confirmHigh, &QCheckBox::toggled, this, &CleanupDialog::updateConfirmationState);
    connect(m_confirmPermanent, &QCheckBox::toggled, this, &CleanupDialog::onOptionsChanged);
    connect(m_typeConfirm, &QLineEdit::textChanged, this, &CleanupDialog::updateConfirmationState);

    refreshPlan();
}

CleanupConfirmation CleanupDialog::currentConfirmation() const {
    CleanupConfirmation confirmation;
    confirmation.actionAcknowledged = m_confirmAction->isChecked();
    confirmation.highRiskReviewed = m_confirmHigh->isChecked();
    confirmation.permanentDelete = m_confirmPermanent->isChecked();
    confirmation.typedConfirmation = m_typeConfirm->text();
    return confirmation;
}

void CleanupDialog::onOptionsChanged() {
    refreshPlan();
}

void CleanupDialog::refreshPlan() {
    m_options.mode = core::CleanupMode::DryRun;
    m_options.allowPermanentDelete = m_confirmPermanent->isChecked();

    m_plan = core::buildCleanupPlan(m_candidates, m_options);

    // ---- Summary ----
    m_summaryLabel->setText(
        tr("<b>%1</b> item(s) will be removed — <b>%2</b> estimated recoverable.")
            .arg(m_plan.actionableCount)
            .arg(formatting::bytes(m_plan.totalBytes)));

    QStringList breakdown;
    for (int i = 0; i < 5; ++i) {
        if (m_plan.countByRisk[static_cast<std::size_t>(i)] == 0) {
            continue;
        }
        breakdown << tr("%1: %2 (%3)")
                         .arg(formatting::riskName(static_cast<core::RiskLevel>(i)))
                         .arg(m_plan.countByRisk[static_cast<std::size_t>(i)])
                         .arg(formatting::bytes(m_plan.bytesByRisk[static_cast<std::size_t>(i)]));
    }
    QString breakdownText = breakdown.isEmpty() ? tr("Nothing is actionable.")
                                                : breakdown.join(QStringLiteral(" · "));
    if (m_plan.skippedCount > 0) {
        breakdownText +=
            tr(" — %1 skipped (protected, missing, or not owned by you)").arg(m_plan.skippedCount);
    }
    m_breakdownLabel->setText(breakdownText);

    const std::size_t trashCount = static_cast<std::size_t>(std::count_if(
        m_plan.items.begin(), m_plan.items.end(), [](const core::CleanupItem& item) {
            return item.action == core::CleanupAction::MoveToTrash;
        }));
    const std::size_t deleteCount = static_cast<std::size_t>(std::count_if(
        m_plan.items.begin(), m_plan.items.end(), [](const core::CleanupItem& item) {
            return item.action == core::CleanupAction::PermanentDelete;
        }));
    QString actionText = tr("Planned: %1 to the trash (recoverable), %2 permanently deleted.")
                             .arg(trashCount)
                             .arg(deleteCount);
    m_actionLabel->setText(actionText);

    // ---- Warning banner ----
    QStringList warnings;
    if (planHasActionableRisk(m_plan, core::RiskLevel::High)) {
        warnings << tr("High risk items may be user data. Review each entry before continuing.");
    }
    if (deleteCount > 0) {
        warnings << tr("Permanent deletion cannot be undone. Trash is the default for "
                       "everything else.");
    }
    m_warningLabel->setText(
        warnings.isEmpty() ? QString()
                           : QStringLiteral("<b style='color:#b00'>⚠ %1</b>")
                                 .arg(warnings.join(QStringLiteral(" "))));
    m_warningLabel->setVisible(!warnings.isEmpty());

    // ---- Item table (riskiest and largest first) ----
    std::vector<std::size_t> order(m_plan.items.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        const auto& left = m_plan.items[a];
        const auto& right = m_plan.items[b];
        if (left.action != right.action) {
            return left.action != core::CleanupAction::Skip; // actionable first
        }
        if (left.risk != right.risk) {
            return static_cast<int>(left.risk) > static_cast<int>(right.risk);
        }
        return left.allocatedSize > right.allocatedSize;
    });

    const int shown = static_cast<int>(std::min<std::size_t>(order.size(), kMaxTableRows));
    m_items->setRowCount(shown);
    for (int row = 0; row < shown; ++row) {
        const auto& item = m_plan.items[order[static_cast<std::size_t>(row)]];
        auto* pathItem = new QTableWidgetItem(QString::fromStdString(item.path.string()));
        pathItem->setToolTip(QString::fromStdString(item.path.string()));
        auto* riskItem = new QTableWidgetItem(formatting::riskName(item.risk));
        riskItem->setForeground(formatting::riskColor(item.risk));
        auto* sizeItem = new QTableWidgetItem(formatting::bytes(item.allocatedSize));
        QString action = actionName(item.action);
        if (item.action == core::CleanupAction::Skip && !item.skipReason.empty()) {
            action += QStringLiteral(" — ") + QString::fromStdString(item.skipReason);
        }
        auto* actionItem = new QTableWidgetItem(action);
        if (item.action == core::CleanupAction::Skip) {
            actionItem->setForeground(QColor(0x88, 0x88, 0x88));
        }
        m_items->setItem(row, 0, pathItem);
        m_items->setItem(row, 1, riskItem);
        m_items->setItem(row, 2, sizeItem);
        m_items->setItem(row, 3, actionItem);
    }
    if (order.size() > kMaxTableRows) {
        m_tableNote->setText(tr("Showing the first %1 of %2 items.")
                                 .arg(kMaxTableRows)
                                 .arg(order.size()));
    } else if (m_plan.skippedCount > 0) {
        m_tableNote->setText(tr("Skipped items are listed for transparency but will never be "
                                "touched."));
    } else {
        m_tableNote->clear();
    }

    // ---- Confirmation widgets ----
    const bool offered = permanentDeleteOffered(m_plan);
    if (!offered && m_confirmPermanent->isChecked()) {
        const QSignalBlocker blocker(m_confirmPermanent);
        m_confirmPermanent->setChecked(false);
    }
    m_confirmPermanent->setEnabled(offered);
    m_confirmPermanent->setVisible(offered);

    m_confirmAction->setText(
        tr("I understand this run will remove %1 item(s) from disk.").arg(m_plan.actionableCount));

    const bool highActionable = planHasActionableRisk(m_plan, core::RiskLevel::High);
    m_confirmHigh->setVisible(highActionable);
    if (!highActionable && m_confirmHigh->isChecked()) {
        const QSignalBlocker blocker(m_confirmHigh);
        m_confirmHigh->setChecked(false);
    }

    updateConfirmationState();
}

void CleanupDialog::updateConfirmationState() {
    const CleanupConfirmation confirmation = currentConfirmation();
    const bool typeNeeded = typeConfirmationRequired(m_plan, confirmation);
    m_typeRow->setVisible(typeNeeded);

    const bool enabled = applyEnabled(m_plan, confirmation);
    m_applyButton->setEnabled(enabled);
    m_dryRunButton->setEnabled(m_plan.actionableCount > 0);

    if (enabled) {
        m_applyButton->setToolTip(tr("Review is complete; this will modify files."));
    } else if (m_plan.actionableCount == 0) {
        m_applyButton->setToolTip(tr("Nothing to clean up."));
    } else if (!confirmation.actionAcknowledged) {
        m_applyButton->setToolTip(tr("Tick the acknowledgement to continue."));
    } else if (planHasActionableRisk(m_plan, core::RiskLevel::High) &&
               !confirmation.highRiskReviewed) {
        m_applyButton->setToolTip(tr("Review the high risk items and tick the box."));
    } else if (typeNeeded) {
        m_applyButton->setToolTip(tr("Type %1 to confirm.").arg(QLatin1String(kTypeWord)));
    }
}

void CleanupDialog::setBusy(bool busy) {
    m_dryRunButton->setEnabled(!busy);
    m_applyButton->setEnabled(!busy);
    m_closeButton->setEnabled(!busy);
    m_confirmAction->setEnabled(!busy);
    m_confirmHigh->setEnabled(!busy);
    m_confirmPermanent->setEnabled(!busy && permanentDeleteOffered(m_plan));
    if (busy) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void CleanupDialog::runDryRun() {
    m_options.mode = core::CleanupMode::DryRun;
    m_options.allowPermanentDelete = m_confirmPermanent->isChecked();

    setBusy(true);
    const core::CleanupReport report = core::executeCleanup(m_plan, m_options);
    setBusy(false);

    // Dry runs are recorded too: the log shows what was previewed.
    utils::AuditLog log(utils::AuditLog::defaultPath());
    log.append(utils::makeCleanupResultEntry(m_plan, report, currentUser().toStdString()));

    showReport(report);
    updateConfirmationState();
}

void CleanupDialog::runApply() {
    const CleanupConfirmation confirmation = currentConfirmation();
    if (!applyEnabled(m_plan, confirmation)) {
        return;
    }

    const std::size_t deleteCount = static_cast<std::size_t>(std::count_if(
        m_plan.items.begin(), m_plan.items.end(), [](const core::CleanupItem& item) {
            return item.action == core::CleanupAction::PermanentDelete;
        }));

    QString question =
        tr("Remove %1 item(s)?\n\n").arg(m_plan.actionableCount);
    if (deleteCount > 0) {
        question += tr("Eligible Safe caches (%1) are PERMANENTLY deleted and cannot be "
                       "recovered. Everything else is moved to the trash.")
                        .arg(deleteCount);
    } else {
        question += tr("All items are moved to the trash and can be restored from your file "
                       "manager.");
    }
    if (QMessageBox::warning(this, tr("Confirm cleanup"), question,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }

    applyConfirmed();
}

void CleanupDialog::applyConfirmed() {
    const CleanupConfirmation confirmation = currentConfirmation();
    if (!applyEnabled(m_plan, confirmation)) {
        return;
    }

    m_options.mode = core::CleanupMode::Apply;
    m_options.allowPermanentDelete = confirmation.permanentDelete;

    const std::string user = currentUser().toStdString();
    utils::AuditLog log(utils::AuditLog::defaultPath());

    // No unaudited destructive action: refuse to run if the "before" record
    // cannot be written.
    if (!log.append(utils::makeCleanupBeginEntry(m_plan, user))) {
        m_report->setHtml(
            tr("<p style='color:#b00'><b>Cleanup did not run.</b> The audit log %1 could not "
               "be written; nothing was modified.</p>")
                .arg(QString::fromStdString(log.path().string())));
        return;
    }

    setBusy(true);
    const core::CleanupReport report = core::executeCleanup(m_plan, m_options);
    setBusy(false);

    log.append(utils::makeCleanupResultEntry(m_plan, report, user));

    m_applied = report.moved > 0 || report.deleted > 0;
    showReport(report);

    if (m_applied) {
        // The plan is stale now; force a fresh selection in the main window.
        m_applyButton->setEnabled(false);
        m_dryRunButton->setEnabled(false);
        m_closeButton->setText(tr("Close and rescan"));
    } else {
        updateConfirmationState();
    }
}

void CleanupDialog::showReport(const core::CleanupReport& report) {
    const bool dryRun = report.mode == core::CleanupMode::DryRun;

    QString html;
    if (dryRun) {
        const std::size_t plannedTrash = static_cast<std::size_t>(std::count_if(
            m_plan.items.begin(), m_plan.items.end(), [](const core::CleanupItem& item) {
                return item.action == core::CleanupAction::MoveToTrash;
            }));
        const std::size_t plannedDelete = static_cast<std::size_t>(std::count_if(
            m_plan.items.begin(), m_plan.items.end(), [](const core::CleanupItem& item) {
                return item.action == core::CleanupAction::PermanentDelete;
            }));
        html += tr("<p><b>Dry run complete — nothing was changed.</b></p>");
        html += tr("<p>Would move %1 item(s) to the trash and permanently delete %2 "
                   "(%3 total).</p>")
                    .arg(plannedTrash)
                    .arg(plannedDelete)
                    .arg(formatting::bytes(report.bytesPlanned));
    } else {
        html += report.ok() ? tr("<p><b>Cleanup complete.</b></p>")
                            : tr("<p><b>Cleanup finished with problems.</b></p>");
        html += QStringLiteral("<ul>");
        html += tr("<li>Moved to trash: %1 (%2, recoverable)</li>")
                    .arg(report.moved)
                    .arg(formatting::bytes(report.bytesTrashed));
        html += tr("<li>Permanently deleted: %1 (%2)</li>")
                    .arg(report.deleted)
                    .arg(formatting::bytes(report.bytesFreed));
        html += tr("<li>Skipped: %1</li>").arg(report.skipped);
        html += tr("<li>Failed: %1</li>").arg(report.failed);
        html += QStringLiteral("</ul>");
    }

    if (report.aborted) {
        html += tr("<p style='color:#b00'><b>Batch aborted:</b> %1</p>")
                    .arg(QString::fromStdString(report.abortReason));
    }

    // Surface failures (partial-failure reporting) — up to 20 entries.
    int shown = 0;
    for (const auto& item : report.items) {
        if (item.status != core::CleanupStatus::Failed || shown >= 20) {
            continue;
        }
        html += tr("<div style='color:#b00'>%1 — %2</div>")
                    .arg(QString::fromStdString(item.path.string()),
                         QString::fromStdString(item.detail));
        ++shown;
    }
    if (report.failed > static_cast<std::size_t>(shown)) {
        html += tr("<div>… and %1 more failure(s) in the audit log.</div>")
                    .arg(report.failed - static_cast<std::size_t>(shown));
    }

    html += tr("<p style='color:#888'>Audit log: %1 · %2 ms</p>")
                .arg(QString::fromStdString(utils::AuditLog::defaultPath().string()))
                .arg(report.duration.count());

    if (!dryRun && m_applied) {
        html += tr("<p>Close this window to rescan and refresh the results.</p>");
    }

    m_report->setHtml(html);
}

} // namespace driverse::gui
