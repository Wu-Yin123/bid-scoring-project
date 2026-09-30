#include "rule_library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QSet>

namespace {
QJsonArray stringArray(const QStringList& values)
{
    return QJsonArray::fromStringList(values);
}

QStringList arrayStrings(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString();
        if (!text.isEmpty() && !result.contains(text)) result.append(text);
    }
    return result;
}

QStringList mergedStrings(const QJsonValue& left, const QStringList& right)
{
    QStringList result = arrayStrings(left);
    for (const QString& value : right) {
        if (!value.isEmpty() && !result.contains(value)) result.append(value);
    }
    return result;
}
}

QString RuleLibraryStore::nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString RuleLibraryStore::normalizeText(const QString& text)
{
    QString normalized = text.normalized(QString::NormalizationForm_KC).toLower().simplified();
    normalized.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
    return normalized;
}

QString RuleLibraryStore::contentHash(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(normalizeText(text).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonArray RuleLibraryStore::uniqueArray(const QJsonArray& values)
{
    QJsonArray result;
    QSet<QString> seen;
    for (const QJsonValue& value : values) {
        const QString key = value.toString();
        if (key.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        result.append(key);
    }
    return result;
}

QJsonObject RuleLibraryStore::sourceObject(const ClauseRecord& clause)
{
    QJsonObject source;
    source[QStringLiteral("file")] = clause.sourceFile;
    source[QStringLiteral("page")] = clause.sourcePage;
    source[QStringLiteral("page_end")] = clause.sourcePageEnd;
    source[QStringLiteral("article")] = clause.article;
    source[QStringLiteral("source_rule_id")] = clause.ruleId;
    return source;
}

bool RuleLibraryStore::sameSource(const QJsonObject& left, const QJsonObject& right)
{
    return left.value(QStringLiteral("file")).toString() == right.value(QStringLiteral("file")).toString()
        && left.value(QStringLiteral("page")).toInt() == right.value(QStringLiteral("page")).toInt()
        && left.value(QStringLiteral("page_end")).toInt() == right.value(QStringLiteral("page_end")).toInt()
        && left.value(QStringLiteral("article")).toString() == right.value(QStringLiteral("article")).toString();
}

bool RuleLibraryStore::readJson(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开规则库文件：") + path;
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("规则库 JSON 无效：") + parseError.errorString();
        return false;
    }
    *object = document.object();
    return true;
}

bool RuleLibraryStore::writeJson(const QString& path, const QJsonObject& object, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入规则库文件：") + path;
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("无法提交规则库文件：") + path;
        return false;
    }
    return true;
}

QJsonObject RuleLibraryStore::emptyIndex(const QString& categoryCode) const
{
    QJsonObject index;
    index[QStringLiteral("schema_version")] = QStringLiteral("1.0");
    index[QStringLiteral("category_code")] = categoryCode;
    index[QStringLiteral("category_name")] = m_categoryNames.value(categoryCode).toString();
    index[QStringLiteral("updated_at")] = QString();
    index[QStringLiteral("rule_ids")] = QJsonArray();
    index[QStringLiteral("subcategory_rule_ids")] = QJsonObject();
    index[QStringLiteral("professional_tag_rule_ids")] = QJsonObject();
    index[QStringLiteral("scene_rule_ids")] = QJsonObject();
    return index;
}

bool RuleLibraryStore::open(const QString& masterPath, const QString& indexDirectory,
                            const QJsonObject& categoryNames, QString* error)
{
    m_masterPath = QDir::cleanPath(masterPath);
    m_indexDirectory = QDir::cleanPath(indexDirectory);
    m_categoryNames = categoryNames;
    m_indexes.clear();

    if (QFileInfo::exists(m_masterPath)) {
        if (!readJson(m_masterPath, &m_master, error)) return false;
        if (!m_master.value(QStringLiteral("rules")).isArray()) {
            if (error) *error = QStringLiteral("主规则库缺少 rules 数组：") + m_masterPath;
            return false;
        }
        // Keep legacy libraries readable while advertising the richer master
        // contract on the next save.
        if (m_master.value(QStringLiteral("schema_version")).toString() == QStringLiteral("1.0"))
            m_master[QStringLiteral("schema_version")] = QStringLiteral("regulation-master.v1");
    } else {
        m_master = QJsonObject{
            {QStringLiteral("schema_version"), QStringLiteral("regulation-master.v1")},
            {QStringLiteral("library_name"), QStringLiteral("工程建设项目法规标准主规则库")},
            {QStringLiteral("updated_at"), QString()},
            {QStringLiteral("rules"), QJsonArray()}
        };
    }

    QStringList categories = m_categoryNames.keys();
    categories.sort();
    for (const QString& category : categories) {
        const QString path = QDir(m_indexDirectory).filePath(category + QStringLiteral(".json"));
        QJsonObject index;
        if (QFileInfo::exists(path)) {
            if (!readJson(path, &index, error)) return false;
            if (!index.value(QStringLiteral("rule_ids")).isArray()) {
                if (error) *error = QStringLiteral("分类索引缺少 rule_ids 数组：") + path;
                return false;
            }
        } else {
            index = emptyIndex(category);
        }
        m_indexes.insert(category, index);
    }
    // Rebuild index membership from the master on every open.  This removes
    // stale entries from older library versions and keeps subtype membership
    // scoped to its P01-P09 parent category.
    for (auto iterator = m_indexes.begin(); iterator != m_indexes.end(); ++iterator) {
        QJsonObject index = iterator.value();
        index[QStringLiteral("rule_ids")] = QJsonArray();
        index[QStringLiteral("subcategory_rule_ids")] = QJsonObject();
        index[QStringLiteral("professional_tag_rule_ids")] = QJsonObject();
        index[QStringLiteral("scene_rule_ids")] = QJsonObject();
        iterator.value() = index;
    }
    for (const QJsonValue& value : m_master.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject rule = value.toObject();
        addToIndexes(rule.value(QStringLiteral("rule_id")).toString(),
                     arrayStrings(rule.value(QStringLiteral("categories"))),
                     arrayStrings(rule.value(QStringLiteral("subcategory_codes"))),
                     arrayStrings(rule.value(QStringLiteral("professional_tag_codes"))),
                     arrayStrings(rule.value(QStringLiteral("scene_codes"))));
    }
    m_open = true;
    return true;
}

void RuleLibraryStore::addToIndexes(const QString& libraryId, const QStringList& categories,
                                    const QStringList& subcategories,
                                    const QStringList& professionalTags,
                                    const QStringList& scenes)
{
    const auto addNested = [&libraryId](QJsonObject* index, const QString& key, const QStringList& values) {
        if (!index) return;
        QJsonObject nested = index->value(key).toObject();
        for (const QString& value : values) {
            if (value.isEmpty()) continue;
            QJsonArray ids = nested.value(value).toArray();
            bool exists = false;
            for (const QJsonValue& id : ids) if (id.toString() == libraryId) { exists = true; break; }
            if (!exists) ids.append(libraryId);
            nested[value] = ids;
        }
        (*index)[key] = nested;
    };
    for (const QString& category : categories) {
        if (!m_indexes.contains(category)) continue;
        QJsonObject index = m_indexes.value(category);
        QStringList categorySubcategories;
        for (const QString& subcategory : subcategories) {
            if (subcategory.startsWith(category + QChar('-'))) categorySubcategories.append(subcategory);
        }
        QJsonArray ids = index.value(QStringLiteral("rule_ids")).toArray();
        bool exists = false;
        for (const QJsonValue& value : ids) {
            if (value.toString() == libraryId) {
                exists = true;
                break;
            }
        }
        if (!exists) ids.append(libraryId);
        index[QStringLiteral("rule_ids")] = ids;
        addNested(&index, QStringLiteral("subcategory_rule_ids"), categorySubcategories);
        addNested(&index, QStringLiteral("professional_tag_rule_ids"), professionalTags);
        addNested(&index, QStringLiteral("scene_rule_ids"), scenes);
        index[QStringLiteral("updated_at")] = nowIso();
        m_indexes[category] = index;
    }
}

LibraryInsertResult RuleLibraryStore::addClause(const ClauseRecord& clause, QString* error, bool persist)
{
    LibraryInsertResult result;
    if (!m_open) {
        result.message = QStringLiteral("规则库尚未打开");
        if (error) *error = result.message;
        return result;
    }
    const QString normalized = normalizeText(clause.text);
    if (normalized.isEmpty()) {
        // Punctuation-only, page-number and whitespace fragments are extraction
        // noise. Treat them as invalid without surfacing a persistence error.
        result.message = QStringLiteral("已跳过无实质内容的条款片段");
        return result;
    }
    const QString hash = contentHash(clause.text);
    QJsonArray rules = m_master.value(QStringLiteral("rules")).toArray();
    const QJsonObject source = sourceObject(clause);
    for (int index = 0; index < rules.size(); ++index) {
        QJsonObject rule = rules.at(index).toObject();
        if (rule.value(QStringLiteral("content_hash")).toString() != hash) continue;

        QJsonArray sources = rule.value(QStringLiteral("sources")).toArray();
        bool sourceExists = false;
        for (const QJsonValue& value : sources) {
            if (sameSource(value.toObject(), source)) {
                sourceExists = true;
                break;
            }
        }
        if (!sourceExists) sources.append(source);
        rule[QStringLiteral("sources")] = sources;
        rule[QStringLiteral("categories")] = stringArray(mergedStrings(rule.value(QStringLiteral("categories")), clause.categories));
        rule[QStringLiteral("category_names")] = stringArray(mergedStrings(rule.value(QStringLiteral("category_names")), clause.categoryNames));
        rule[QStringLiteral("subcategory_codes")] = stringArray(mergedStrings(rule.value(QStringLiteral("subcategory_codes")), clause.subcategories));
        rule[QStringLiteral("professional_tag_codes")] = stringArray(mergedStrings(rule.value(QStringLiteral("professional_tag_codes")), clause.professionalTagCodes));
        rule[QStringLiteral("scene_codes")] = stringArray(mergedStrings(rule.value(QStringLiteral("scene_codes")), clause.sceneCodes));
        rule[QStringLiteral("dimension_codes")] = stringArray(mergedStrings(rule.value(QStringLiteral("dimension_codes")), clause.dimensionCodes));
        rule[QStringLiteral("dimensions")] = stringArray(mergedStrings(rule.value(QStringLiteral("dimensions")), clause.dimensions));
        if (rule.value(QStringLiteral("document_code")).toString().isEmpty()) rule[QStringLiteral("document_code")] = clause.documentCode;
        if (rule.value(QStringLiteral("document_title")).toString().isEmpty()) rule[QStringLiteral("document_title")] = clause.documentTitle;
        if (rule.value(QStringLiteral("issued_by")).toString().isEmpty()) rule[QStringLiteral("issued_by")] = clause.issuedBy;
        if (rule.value(QStringLiteral("document_level")).toString().isEmpty()) rule[QStringLiteral("document_level")] = clause.documentLevel;
        rule[QStringLiteral("last_seen_at")] = nowIso();
        rules[index] = rule;
        m_master[QStringLiteral("rules")] = rules;
        addToIndexes(rule.value(QStringLiteral("rule_id")).toString(), arrayStrings(rule.value(QStringLiteral("categories"))),
                     arrayStrings(rule.value(QStringLiteral("subcategory_codes"))),
                     arrayStrings(rule.value(QStringLiteral("professional_tag_codes"))),
                     arrayStrings(rule.value(QStringLiteral("scene_codes"))));
        if (persist && !save(error)) {
            result.message = error ? *error : QStringLiteral("保存主规则库失败");
            return result;
        }
        result.status = LibraryInsertStatus::ExactDuplicate;
        result.libraryId = rule.value(QStringLiteral("rule_id")).toString();
        result.message = QStringLiteral("发现完全相同的规则，已合并来源");
        return result;
    }

    const QString libraryId = QStringLiteral("RL-") + hash.left(12).toUpper();
    QJsonObject rule = clause.toJson();
    rule[QStringLiteral("rule_id")] = libraryId;
    rule[QStringLiteral("source_rule_id")] = clause.ruleId;
    rule[QStringLiteral("content_hash")] = hash;
    rule[QStringLiteral("normalized_text")] = normalized;
    rule[QStringLiteral("title")] = clause.title;
    rule[QStringLiteral("categories")] = stringArray(clause.categories);
    rule[QStringLiteral("subcategories")] = stringArray(clause.subcategories);
    rule[QStringLiteral("subcategory_codes")] = stringArray(clause.subcategories);
    rule[QStringLiteral("professional_tag_codes")] = stringArray(clause.professionalTagCodes);
    rule[QStringLiteral("scene_codes")] = stringArray(clause.sceneCodes);
    rule[QStringLiteral("dimension_codes")] = stringArray(clause.dimensionCodes);
    rule[QStringLiteral("status")] = QStringLiteral("pending_review");
    rule[QStringLiteral("version")] = 1;
    rule[QStringLiteral("sources")] = QJsonArray{source};
    rule[QStringLiteral("first_seen_at")] = nowIso();
    rule[QStringLiteral("last_seen_at")] = nowIso();
    rule[QStringLiteral("supersedes")] = QString();
    rule[QStringLiteral("similar_rule_ids")] = QJsonArray();
    rule[QStringLiteral("notes")] = QString();
    rules.append(rule);
    m_master[QStringLiteral("rules")] = rules;
    addToIndexes(libraryId, clause.categories, clause.subcategories, clause.professionalTagCodes, clause.sceneCodes);
    if (persist && !save(error)) {
        result.message = error ? *error : QStringLiteral("保存主规则库失败");
        return result;
    }
    result.status = LibraryInsertStatus::Added;
    result.libraryId = libraryId;
    result.message = QStringLiteral("已新增规则");
    return result;
}

bool RuleLibraryStore::save(QString* error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("规则库尚未打开");
        return false;
    }
    m_master[QStringLiteral("updated_at")] = nowIso();
    if (!writeJson(m_masterPath, m_master, error)) return false;
    for (auto iterator = m_indexes.cbegin(); iterator != m_indexes.cend(); ++iterator) {
        const QString path = QDir(m_indexDirectory).filePath(iterator.key() + QStringLiteral(".json"));
        if (!writeJson(path, iterator.value(), error)) return false;
    }
    return true;
}

int RuleLibraryStore::ruleCount() const
{
    return m_master.value(QStringLiteral("rules")).toArray().size();
}

QJsonObject RuleLibraryStore::indexJson(const QString& categoryCode) const
{
    return m_indexes.value(categoryCode);
}
