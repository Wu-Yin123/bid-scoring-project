#pragma once

#include "models.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class RuleEngine
{
public:
    bool load(const QString& configPath, QString* error = nullptr, const QString& taxonomyPath = QString());
    ClauseRecord classify(const QString& sourceFile, int page, const QString& article,
                          const QString& text, int ordinal) const;
    QStringList categoryCodes() const;
    QString categoryName(const QString& code) const;

private:
    QJsonObject m_config;
    QJsonObject m_categories;
    QJsonArray m_taxonomyCategories;
    QJsonArray m_taxonomyTags;
    QJsonArray m_taxonomyScenes;
    QJsonObject m_dimensions;
    QJsonObject m_obligationPatterns;
    QStringList m_evidenceKeywords;

    static int score(const QString& text, const QJsonArray& keywords, QStringList* matched);
    static QStringList unique(const QStringList& values);
    static QStringList stringArray(const QJsonValue& value);
    QString titleFor(const QString& text, const QString& article) const;
    QStringList extractConditions(const QString& text) const;
    QStringList extractEvidence(const QString& text) const;
    QString obligationFor(const QString& text) const;
    void classifyTaxonomy(const QString& text, ClauseRecord* record) const;
};
