#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QDialog>
#include <QString>

class QWidget;
class QListWidgetItem;

namespace Ui {
class RuleLibraryDialog;
}

class RuleLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RuleLibraryDialog(QWidget* parent = nullptr,
                               const QString& libraryPath = QString(),
                               const QString& indexDirectory = QString());
    ~RuleLibraryDialog() override;

private:
    Ui::RuleLibraryDialog* ui = nullptr;
    QJsonArray m_rules;
    QJsonObject m_categoryNames;
    QString m_libraryPath;
    QString m_indexDirectory;

    QString findProjectRoot() const;
    bool loadLibrary(QString* error = nullptr);
    void reloadLibrary(bool showError);
    void populateFilters();
    void refreshList();
    void showRule(const QListWidgetItem* item);
    void clearDetails();
};
