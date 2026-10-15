#pragma once

#include <QFrame>

class QLabel;

namespace driverse::gui {

// A labelled value card used in the dashboard summary row.
class SummaryCard : public QFrame {
    Q_OBJECT

public:
    explicit SummaryCard(const QString& title, QWidget* parent = nullptr);

    void setValue(const QString& value);
    void setAccent(bool on); // highlight the value (e.g. recoverable space)

private:
    QLabel* m_title;
    QLabel* m_value;
};

} // namespace driverse::gui
