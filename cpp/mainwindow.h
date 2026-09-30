#pragma once

#include "models.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QThread;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void addFiles();
    void addFolder();
    void removeSelected();
    void clearFiles();
    void browseOutput();
    void browseConfig();
    void browsePdfTool();
    void browseSevenZip();
    void browsePackage();
    void startProcessing();
    void openReview();
    void openRuleLibrary();
    void handleFinished(const BatchResult& result);
    void openOutputDirectory();

private:
    Ui::MainWindow* ui = nullptr;
    QListWidget* m_files = nullptr;
    QLineEdit* m_output = nullptr;
    QLineEdit* m_config = nullptr;
    QLineEdit* m_pdfTool = nullptr;
    QLineEdit* m_sevenZip = nullptr;
    QLineEdit* m_package = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_openOutput = nullptr;
    QThread* m_thread = nullptr;

    QString findProjectRoot() const;
    void appendFiles(const QStringList& paths);
    QStringList selectedFiles() const;
    void setBusy(bool busy);
};
