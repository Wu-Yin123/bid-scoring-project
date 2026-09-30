#pragma once

#include <QJsonObject>
#include <QStringList>

struct ClauseRecord {
    QString ruleId;
    QString sourceFile;
    // Optional document metadata.  The parser fills what it can infer; the
    // exporter keeps these fields so a later review/import step can complete
    // them without changing the master rule shape.
    QString documentCode;
    QString documentTitle;
    QString issuedBy;
    QString documentLevel;
    int sourcePage = 0;
    int sourcePageEnd = 0;
    QString article;
    QString text;
    QString title;
    QStringList categories;
    QStringList categoryNames;
    QStringList subcategories;
    QStringList professionalTagCodes;
    QStringList sceneCodes;
    QString categoryConfidence;
    QString classificationStatus;
    QJsonObject categoryEvidence;
    QStringList dimensions;
    QStringList dimensionCodes;
    QString obligationLevel;
    QStringList conditions;
    QStringList evidence;
    QStringList matchedKeywords;
    QStringList reviewFlags;
    QString priority;

    QJsonObject toJson() const;
};

struct DocumentResult {
    QString sourceFile;
    QString sourcePath;
    QString suffix;
    int pageCount = 0;
    QString status;
    QString outputBase;
    QString sourceEncoding;
    QString extractedText;
    QStringList warnings;
    QList<ClauseRecord> rules;
    int libraryAdded = 0;
    int libraryDuplicates = 0;
    int libraryErrors = 0;
    int skippedNoise = 0;
};

struct BatchResult {
    QList<DocumentResult> documents;
    QStringList errors;
    QString libraryPath;
    QString indexDirectory;
    int libraryAdded = 0;
    int libraryDuplicates = 0;
    int libraryErrors = 0;
    int skippedNoise = 0;
    QString packagePath;
    int packageExported = 0;
    int packageEntries = 0;
    int packageSkipped = 0;
    QStringList packageWarnings;
    QStringList packageErrors;
};

Q_DECLARE_METATYPE(BatchResult)
