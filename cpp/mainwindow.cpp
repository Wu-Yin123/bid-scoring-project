#include "mainwindow.h"
#include "batch_worker.h"
#include "review_dialog.h"
#include "rule_library_dialog.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->rootLayout->setStretch(2, 2);
    ui->rootLayout->setStretch(5, 1);
    ui->actionLayout->setStretch(4, 1);

    m_files = ui->fileList;
    m_output = ui->outputEdit;
    m_config = ui->configEdit;
    m_pdfTool = ui->pdfToolEdit;
    m_sevenZip = ui->sevenZipEdit;
    m_package = ui->packageEdit;
    m_log = ui->logEdit;
    m_progress = ui->progressBar;
    m_status = ui->statusLabel;
    m_start = ui->startButton;
    m_openOutput = ui->openOutputButton;

    const QString projectRoot = findProjectRoot();
    m_output->setText(QDir(projectRoot).filePath(QStringLiteral("output_cpp")));
    m_config->setText(QDir(projectRoot).filePath(QStringLiteral("config/rules.json")));
    m_package->setText(QDir(projectRoot).filePath(QStringLiteral("export/inspection-and-review-system")));
    const QString foundPdf = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (!foundPdf.isEmpty()) m_pdfTool->setText(foundPdf);
    const QString sevenZip = QStringLiteral("D:/software/7Zip/7-Zip/7z.exe");
    if (QFileInfo::exists(sevenZip)) m_sevenZip->setText(QDir::toNativeSeparators(sevenZip));

    ui->settingsLayout->setColumnStretch(1, 1);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    connect(ui->addFilesButton, &QPushButton::clicked, this, &MainWindow::addFiles);
    connect(ui->addFolderButton, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(ui->clearButton, &QPushButton::clicked, this, &MainWindow::clearFiles);
    connect(ui->outputBrowseButton, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(ui->configBrowseButton, &QPushButton::clicked, this, &MainWindow::browseConfig);
    connect(ui->pdfToolBrowseButton, &QPushButton::clicked, this, &MainWindow::browsePdfTool);
    connect(ui->sevenZipBrowseButton, &QPushButton::clicked, this, &MainWindow::browseSevenZip);
    connect(ui->packageBrowseButton, &QPushButton::clicked, this, &MainWindow::browsePackage);
    connect(m_start, &QPushButton::clicked, this, &MainWindow::startProcessing);
    connect(ui->reviewButton, &QPushButton::clicked, this, &MainWindow::openReview);
    connect(ui->libraryButton, &QPushButton::clicked, this, &MainWindow::openRuleLibrary);
    connect(m_openOutput, &QPushButton::clicked, this, &MainWindow::openOutputDirectory);
}

MainWindow::~MainWindow()
{
    delete ui;
}

QString MainWindow::findProjectRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 8; ++level) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("config/rules.json")))) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    const QString preferred = QStringLiteral("D:/Code/Codex/标书评分项目");
    if (QFileInfo::exists(QDir(preferred).filePath(QStringLiteral("config/rules.json")))) return QDir::toNativeSeparators(preferred);
    return QDir::currentPath();
}

void MainWindow::appendFiles(const QStringList& paths)
{
    QStringList existing;
    for (int index = 0; index < m_files->count(); ++index) existing.append(m_files->item(index)->text());
    for (const QString& path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!existing.contains(absolute, Qt::CaseInsensitive)) {
            m_files->addItem(absolute);
            existing.append(absolute);
        }
    }
    m_status->setText(QStringLiteral("已添加 %1 个文件").arg(m_files->count()));
}

QStringList MainWindow::selectedFiles() const
{
    QStringList result;
    for (int index = 0; index < m_files->count(); ++index) result.append(m_files->item(index)->text());
    return result;
}

void MainWindow::addFiles()
{
    appendFiles(QFileDialog::getOpenFileNames(this, QStringLiteral("选择法规或标准文件"), QString(),
                                              QStringLiteral("支持的文件 (*.pdf *.docx *.txt *.md *.markdown);;所有文件 (*.*)")));
}

void MainWindow::addFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择法规或标准文件夹"));
    if (directory.isEmpty()) return;
    QStringList files;
    QDirIterator iterator(directory, {QStringLiteral("*.pdf"), QStringLiteral("*.docx"), QStringLiteral("*.txt"),
                                      QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) files.append(iterator.next());
    appendFiles(files);
}

void MainWindow::removeSelected()
{
    const QList<QListWidgetItem*> items = m_files->selectedItems();
    for (QListWidgetItem* item : items) delete m_files->takeItem(m_files->row(item));
    m_status->setText(QStringLiteral("剩余 %1 个文件").arg(m_files->count()));
}

void MainWindow::clearFiles()
{
    m_files->clear();
    m_status->setText(QStringLiteral("等待添加文件"));
}

void MainWindow::browseOutput()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"), m_output->text());
    if (!path.isEmpty()) m_output->setText(path);
}

void MainWindow::browseConfig()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择分类规则"), m_config->text(), QStringLiteral("JSON 文件 (*.json)"));
    if (!path.isEmpty()) m_config->setText(path);
}

void MainWindow::browsePdfTool()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 pdftotext.exe"), QString(), QStringLiteral("可执行文件 (*.exe)"));
    if (!path.isEmpty()) m_pdfTool->setText(path);
}

void MainWindow::browseSevenZip()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 7z.exe"), m_sevenZip->text(), QStringLiteral("可执行文件 (*.exe)"));
    if (!path.isEmpty()) m_sevenZip->setText(path);
}

void MainWindow::browsePackage()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择运行配置包目录（D:\\Code\\Codex）"), m_package->text());
    if (!path.isEmpty()) m_package->setText(path);
}

void MainWindow::setBusy(bool busy)
{
    m_start->setEnabled(!busy);
    m_files->setEnabled(!busy);
    if (!busy) m_progress->setValue(m_progress->maximum());
}

void MainWindow::startProcessing()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("没有文件"), QStringLiteral("请先添加一个或多个法规、标准文件。"));
        return;
    }
    if (!QFileInfo::exists(m_config->text())) {
        QMessageBox::warning(this, QStringLiteral("配置不存在"), QStringLiteral("分类规则文件不存在，请重新选择。"));
        return;
    }
    if (m_output->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("缺少输出目录"), QStringLiteral("请选择输出目录。"));
        return;
    }
    QString packagePath = QDir::cleanPath(m_package->text().trimmed());
    QString packagePathForCheck = packagePath;
    packagePathForCheck.replace(QChar('\\'), QChar('/'));
    if (packagePathForCheck.compare(QStringLiteral("D:/Code/Codex"), Qt::CaseInsensitive) != 0
        && !packagePathForCheck.startsWith(QStringLiteral("D:/Code/Codex/"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, QStringLiteral("运行包路径不符合要求"),
                             QStringLiteral("运行配置包必须保存到 D:\\Code\\Codex 下。"));
        return;
    }

    ProcessorOptions options;
    options.outputDirectory = QDir::cleanPath(m_output->text());
    options.configPath = QDir::cleanPath(m_config->text());
    options.pdfToTextPath = QDir::cleanPath(m_pdfTool->text());
    options.sevenZipPath = QDir::cleanPath(m_sevenZip->text());
    const QString projectRoot = findProjectRoot();
    options.libraryPath = QDir(projectRoot).filePath(QStringLiteral("data/rule_library.json"));
    options.indexDirectory = QDir(projectRoot).filePath(QStringLiteral("data/indexes"));
    options.classificationPath = QDir(projectRoot).filePath(QStringLiteral("config/classification/project_categories.json"));
    options.packageOutputDirectory = packagePath;

    m_log->clear();
    m_progress->setRange(0, files.size());
    m_progress->setValue(0);
    m_status->setText(QStringLiteral("正在处理"));
    setBusy(true);

    m_thread = new QThread(this);
    auto* worker = new BatchWorker(files, options);
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, worker, &BatchWorker::run);
    connect(worker, &BatchWorker::progress, this, [this](const QString& message, int current, int total) {
        m_progress->setRange(0, total);
        m_progress->setValue(current);
        m_status->setText(QStringLiteral("%1/%2").arg(current).arg(total));
        m_log->appendPlainText(message);
    });
    connect(worker, &BatchWorker::finished, this, &MainWindow::handleFinished);
    connect(worker, &BatchWorker::finished, m_thread, &QThread::quit);
    connect(worker, &BatchWorker::finished, worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::destroyed, this, [this] { m_thread = nullptr; });
    m_thread->start();
}

void MainWindow::openReview()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开自动分类结果"), m_output->text(),
                                                       QStringLiteral("规则结果 (*.rules.json *.reviewed.json *.json)"));
    if (path.isEmpty()) return;
    ReviewDialog dialog(path, this);
    dialog.exec();
}

void MainWindow::openRuleLibrary()
{
    RuleLibraryDialog dialog(this);
    dialog.exec();
}

void MainWindow::handleFinished(const BatchResult& result)
{
    int ruleCount = 0;
    int warningCount = 0;
    for (const DocumentResult& document : result.documents) {
        ruleCount += document.rules.size();
        warningCount += document.warnings.size();
        for (const QString& warning : document.warnings) m_log->appendPlainText(QStringLiteral("警告 [%1]：%2").arg(document.sourceFile, warning));
    }
    for (const QString& error : result.errors) m_log->appendPlainText(QStringLiteral("错误：") + error);
    warningCount += result.packageWarnings.size();
    m_log->appendPlainText(QStringLiteral("规则库：新增 %1 条，精确重复 %2 条，写入失败 %3 条；跳过噪声 %4 条")
                           .arg(result.libraryAdded)
                           .arg(result.libraryDuplicates)
                           .arg(result.libraryErrors)
                           .arg(result.skippedNoise));
    if (!result.packagePath.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("可直接导入的运行配置包：") + result.packagePath);
        m_log->appendPlainText(QStringLiteral("运行包来源规则：%1；聚合条目：%2；待复核来源：%3")
                               .arg(result.packageExported).arg(result.packageEntries).arg(result.packageSkipped));
    }
    for (const QString& warning : result.packageWarnings) m_log->appendPlainText(QStringLiteral("运行包警告：") + warning);
    for (const QString& error : result.packageErrors) m_log->appendPlainText(QStringLiteral("运行包错误：") + error);
    setBusy(false);
    m_status->setText(QStringLiteral("完成：%1 个文件，%2 条规则，规则库新增 %3 条，跳过噪声 %4 条")
                      .arg(result.documents.size()).arg(ruleCount).arg(result.libraryAdded).arg(result.skippedNoise));
    QMessageBox::information(this, QStringLiteral("批量处理完成"),
                             QStringLiteral("处理文件：%1\n提取规则：%2\n规则库新增：%3\n规则库精确重复：%4\n规则库写入失败：%5\n跳过噪声：%6\n警告：%7\n错误：%8\n\n输出目录：%9")
                             .arg(result.documents.size()).arg(ruleCount)
                             .arg(result.libraryAdded).arg(result.libraryDuplicates).arg(result.libraryErrors)
                             .arg(result.skippedNoise).arg(warningCount).arg(result.errors.size()).arg(m_output->text())
                             + (result.packagePath.isEmpty() ? QString() : QStringLiteral("\n\n可直接导入运行包：") + result.packagePath));
}

void MainWindow::openOutputDirectory()
{
    QDir().mkpath(m_output->text());
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_output->text()));
}
