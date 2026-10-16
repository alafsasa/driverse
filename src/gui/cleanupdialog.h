#pragma once

#include "core/cleanup.h"

#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QWidget;

namespace driverse::gui {

// Confirmation state collected from the dialog. A plain struct so the gating
// rules stay pure and unit-testable without a display.
struct CleanupConfirmation {
    bool actionAcknowledged = false; // "I understand N items will be removed"
    bool highRiskReviewed = false;   // required when a High risk item is actionable
    bool permanentDelete = false;    // opt-in: delete eligible Safe caches
    QString typedConfirmation;       // must equal "DELETE" when required
};

// ---- Risk-based confirmation gates (pure functions) ----

// True if any *actionable* item carries `risk`.
bool planHasActionableRisk(const core::CleanupPlan& plan, core::RiskLevel risk);

// True if the plan contains items eligible for permanent deletion (Safe risk,
// regenerable, opt-in allowed by the recommendation engine).
bool permanentDeleteOffered(const core::CleanupPlan& plan);

// Typing "DELETE" is required when a High risk item is actionable or when
// permanent deletion is enabled (the only two irreversible cases).
bool typeConfirmationRequired(const core::CleanupPlan& plan,
                              const CleanupConfirmation& confirmation);

// Apply is only possible when every applicable gate is satisfied.
bool applyEnabled(const core::CleanupPlan& plan, const CleanupConfirmation& confirmation);

// Phase 10 cleanup workflow: dry-run preview, risk-based confirmation, and a
// guarded executor call. The dialog never touches the filesystem on its own —
// everything goes through core::buildCleanupPlan / core::executeCleanup, and an
// apply is refused if the audit log cannot be written first.
class CleanupDialog : public QDialog {
    Q_OBJECT

public:
    explicit CleanupDialog(std::vector<core::CleanupCandidate> candidates,
                           QWidget* parent = nullptr);

    // True when an apply run actually moved or deleted at least one item.
    bool applied() const { return m_applied; }

    // Runs the cleanup without the interactive confirmation prompt. Used by
    // runApply() after the modal warning and by tests. Refuses to run when the
    // audit log cannot be written (no unaudited destructive action).
    void applyConfirmed();

private slots:
    void runDryRun();
    void runApply();
    void onOptionsChanged();

private:
    void refreshPlan();
    void updateConfirmationState();
    void showReport(const core::CleanupReport& report);
    void setBusy(bool busy);
    CleanupConfirmation currentConfirmation() const;

    std::vector<core::CleanupCandidate> m_candidates;
    core::CleanupOptions m_options;
    core::CleanupPlan m_plan;
    bool m_applied = false;

    QLabel* m_summaryLabel = nullptr;
    QLabel* m_breakdownLabel = nullptr;
    QLabel* m_actionLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QLabel* m_tableNote = nullptr;
    QTableWidget* m_items = nullptr;

    QCheckBox* m_confirmAction = nullptr;
    QCheckBox* m_confirmHigh = nullptr;
    QCheckBox* m_confirmPermanent = nullptr;
    QWidget* m_typeRow = nullptr;
    QLineEdit* m_typeConfirm = nullptr;

    QTextBrowser* m_report = nullptr;
    QPushButton* m_dryRunButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_closeButton = nullptr;
};

} // namespace driverse::gui
