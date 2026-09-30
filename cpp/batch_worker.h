#pragma once

#include "document_processor.h"

#include <QObject>

class BatchWorker : public QObject
{
    Q_OBJECT
public:
    BatchWorker(QStringList files, ProcessorOptions options, QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progress(const QString& message, int current, int total);
    void finished(const BatchResult& result);

private:
    QStringList m_files;
    ProcessorOptions m_options;
};
