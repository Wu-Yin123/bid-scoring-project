#pragma once

#include "models.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

enum class LibraryInsertStatus {
    Added,
    ExactDuplicate,
    Invalid
};

struct LibraryInsertResult {
    LibraryInsertStatus status = LibraryInsertStatus::Invalid;
    QString libraryId;
    QString message;
};

class RuleLibraryStore
{
public:
    bool open(const QString& masterPath, const QString& indexDirectory,
              const QJsonObject& categoryNames, QString* error = nullptr);
    bool save(QString* error = nullptr);

    LibraryInsertResult addClause(const ClauseRecord& clause, QString* error = nullptr, bool persist = true);

    bool isOpen() const { return m_open; }
    int ruleCount() const;
    QJsonObject masterJson() const { return m_master; }
    QJsonObject indexJson(const QString& categoryCode) const;

    static QString normalizeText(const QString& text);
    static QString contentHash(const QString& text);

private:
    QJsonObject m_master;
    QMap<QString, QJsonObject> m_indexes;
    QJsonObject m_categoryNames;
    QString m_masterPath;
    QString m_indexDirectory;
    bool m_open = false;

    static QString nowIso();
    static QJsonArray uniqueArray(const QJsonArray& values);
    static QJsonObject sourceObject(const ClauseRecord& clause);
    static bool sameSource(const QJsonObject& left, const QJsonObject& right);
    static bool readJson(const QString& path, QJsonObject* object, QString* error);
    static bool writeJson(const QString& path, const QJsonObject& object, QString* error);
    QJsonObject emptyIndex(const QString& categoryCode) const;
    void addToIndexes(const QString& libraryId, const QStringList& categories,
                      const QStringList& subcategories = {},
                      const QStringList& professionalTags = {},
                      const QStringList& scenes = {});
};
