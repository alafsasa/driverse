#include "summaryCard.h"

#include <QLabel>
#include <QVBoxLayout>

namespace driverse::gui {

SummaryCard::SummaryCard(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("summaryCard"));
    setStyleSheet(QStringLiteral(
        "#summaryCard { background: #fafafc; border: 1px solid #e2e2e8;"
        " border-radius: 8px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(2);

    m_title = new QLabel(title, this);
    m_title->setStyleSheet(QStringLiteral("color: #86868e; font-size: 11px;"));
    m_value = new QLabel(QStringLiteral("--"), this);

    layout->addWidget(m_title);
    layout->addWidget(m_value);

    setAccent(false); // apply the default value styling
}

void SummaryCard::setValue(const QString& value) {
    m_value->setText(value);
}

void SummaryCard::setAccent(bool on) {
    m_value->setStyleSheet(on ? QStringLiteral("color: #2f8f4e; font-size: 20px;"
                                               " font-weight: 600;")
                              : QStringLiteral("color: #2b2b31; font-size: 20px;"
                                               " font-weight: 600;"));
}

} // namespace driverse::gui
