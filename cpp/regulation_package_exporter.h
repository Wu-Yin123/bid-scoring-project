#pragma once

#include <QStringList>

struct RegulationExportOptions {
    QString masterPath;
    QString taxonomyPath;
    QString outputRoot;
    // Pending machine classifications are exported with review_status metadata
    // so a newly processed document can be consumed immediately.  Callers may
    // turn this off when publishing a reviewed release.
    bool includePending = true;
    bool includeDeprecated = false;
};

struct RegulationExportResult {
    bool success = false;
    QString packageRoot;
    QString manifestPath;
    int exportedRules = 0;
    int runtimeEntries = 0;
    int skippedRules = 0;
    QStringList generatedFiles;
    QStringList warnings;
    QStringList errors;
};

class RegulationPackageExporter
{
public:
    static RegulationExportResult exportPackage(const RegulationExportOptions& options);
};
