#include "mainwindow.h"
#include "document_processor.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QMetaType>

static QString projectRoot()
{
    const QString preferred = QStringLiteral("D:/Code/Codex/标书评分项目");
    if (QFileInfo::exists(QDir(preferred).filePath(QStringLiteral("config/rules.json")))) return preferred;
    return QDir::currentPath();
}

static int runBatchMode(const QStringList& arguments)
{
    ProcessorOptions options;
    const QString root = projectRoot();
    options.outputDirectory = QDir(root).filePath(QStringLiteral("output_cpp"));
    options.configPath = QDir(root).filePath(QStringLiteral("config/rules.json"));
    options.libraryPath = QDir(root).filePath(QStringLiteral("data/rule_library.json"));
    options.indexDirectory = QDir(root).filePath(QStringLiteral("data/indexes"));
    QStringList files;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--output") && index + 1 < arguments.size()) options.outputDirectory = arguments.at(++index);
        else if (argument == QStringLiteral("--config") && index + 1 < arguments.size()) options.configPath = arguments.at(++index);
        else if (argument == QStringLiteral("--pdf-tool") && index + 1 < arguments.size()) options.pdfToTextPath = arguments.at(++index);
        else if (argument == QStringLiteral("--7z") && index + 1 < arguments.size()) options.sevenZipPath = arguments.at(++index);
        else if (argument == QStringLiteral("--library") && index + 1 < arguments.size()) options.libraryPath = arguments.at(++index);
        else if (argument == QStringLiteral("--indexes") && index + 1 < arguments.size()) options.indexDirectory = arguments.at(++index);
        else if (!argument.startsWith(QStringLiteral("--"))) files.append(argument);
    }
    if (files.isEmpty()) {
        qCritical().noquote() << QStringLiteral("批处理模式需要至少一个输入文件。用法：--batch 文件1 文件2 [--output 输出目录] [--library 主规则库] [--indexes 索引目录]");
        return 2;
    }
    DocumentProcessor processor;
    QObject::connect(&processor, &DocumentProcessor::progress, [](const QString& message, int current, int total) {
        qInfo().noquote() << QStringLiteral("[%1/%2] %3").arg(current).arg(total).arg(message);
    });
    const BatchResult result = processor.process(files, options);
    int ruleCount = 0;
    for (const DocumentResult& document : result.documents) ruleCount += document.rules.size();
    qInfo().noquote() << QStringLiteral("处理完成：%1 个文件，%2 条规则；规则库新增 %3，精确重复 %4，写入失败 %5；输出：%6")
                             .arg(result.documents.size()).arg(ruleCount)
                             .arg(result.libraryAdded).arg(result.libraryDuplicates).arg(result.libraryErrors)
                             .arg(options.outputDirectory);
    for (const QString& error : result.errors) qWarning().noquote() << error;
    return result.errors.isEmpty() ? 0 : 1;
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("工程建设项目法规标准批量处理器"));
    application.setOrganizationName(QStringLiteral("标书评分项目"));
    qRegisterMetaType<BatchResult>("BatchResult");
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1 && arguments.at(1) == QStringLiteral("--batch")) return runBatchMode(arguments.mid(1));
    MainWindow window;
    window.show();
    return application.exec();
}


