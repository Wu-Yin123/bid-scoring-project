#include "batch_worker.h"

BatchWorker::BatchWorker(QStringList files, ProcessorOptions options, QObject* parent)
    : QObject(parent), m_files(std::move(files)), m_options(std::move(options))
{
}

void BatchWorker::run()
{
    DocumentProcessor processor;
    connect(&processor, &DocumentProcessor::progress, this, &BatchWorker::progress);
    const BatchResult result = processor.process(m_files, m_options);
    emit finished(result);
}
