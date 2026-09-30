#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Keep the original JSON, including fields added by newer extraction versions.
class ReviewDocument
{
public:
    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr);
    bool setReview(int index, const QStringList& categories, const QString& note,
                   bool confirmed, QString* error = nullptr);
    int count() const { return m_rules.size(); }
    QJsonObject rule(int index) const { return m_rules.at(index).toObject(); }
    QJsonObject categoryNames() const;
    bool isDirty() const { return m_dirty; }
    QString sourcePath() const { return m_sourcePath; }
    QString savedPath() const { return m_savePath; }
    QString suggestedSavePath() const;
    QJsonObject json() const;
    int confirmedCount() const;

private:
    QJsonObject m_root;
    QJsonArray m_rules;
    QString m_sourcePath;
    QString m_originalPath;
    QString m_savePath;
    bool m_dirty = false;
};
