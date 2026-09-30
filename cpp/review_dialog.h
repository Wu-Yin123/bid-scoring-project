#pragma once

#include "review_document.h"
#include <QDialog>

class QWidget;

namespace Ui {
class ReviewDialog;
}

class ReviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReviewDialog(const QString& path, QWidget* parent = nullptr);
    ~ReviewDialog() override;
    void reject() override;

private:
    ReviewDocument m_document;
    Ui::ReviewDialog* ui = nullptr;
    int m_currentIndex = -1;
    bool m_loading = false;

    void showRule(int index);
    void filterRules();
    void updateRow(int index);
    void updateSummary();
    bool editCurrent(bool confirmed);
    bool saveResult(bool saveAs = false);
};
