#pragma once

#include "models.h"
#include "rule_engine.h"

#include <QObject>
#include <QStringList>

struct ProcessorOptions {
    QString outputDirectory;
    QString configPath;
    QString pdfToTextPath;
    QString sevenZipPath;
    QString libraryPath;
    QString indexDirectory;
    // The canonical master remains the source of truth.  After a batch, the
    // same master is exported into this target-compatible package directory.
    QString classificationPath;
    QString packageOutputDirectory;
};

class DocumentProcessor : public QObject
{
    Q_OBJECT
public:
    explicit DocumentProcessor(QObject* parent = nullptr);
    BatchResult process(const QStringList& files, const ProcessorOptions& options);
    static QString resolveTool(const QString& requested, const QStringList& names);
    static bool validateOutputDirectory(const QString& path, QString* error = nullptr);

signals:
    void progress(const QString& message, int current, int total);

private:
    RuleEngine m_engine;

    QString normalize(const QString& value) const;
    QString readPlainText(const QString& path, DocumentResult* result) const;
    QString readDocxText(const QString& path, const QString& sevenZip, DocumentResult* result) const;
    QString readPdfText(const QString& path, const QString& pdfToText, DocumentResult* result) const;
    QString findTool(const QString& requested, const QStringList& names) const;
    QList<QPair<QString, QString>> splitClauses(const QString& text, bool plainText = false) const;
    QList<QString> splitPdfPages(const QString& text) const;
    QStringList writeOutputs(DocumentResult& result, const ProcessorOptions& options) const;
};
