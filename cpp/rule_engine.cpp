#include "rule_engine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <algorithm>

QStringList RuleEngine::stringArray(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& item : value.toArray()) {
        result.append(item.toString());
    }
    return result;
}

QStringList RuleEngine::unique(const QStringList& values)
{
    QStringList result;
    for (const QString& value : values) {
        if (!value.isEmpty() && !result.contains(value)) {
            result.append(value);
        }
    }
    return result;
}

bool RuleEngine::load(const QString& configPath, QString* error, const QString& taxonomyPathOverride)
{
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开分类规则：") + configPath;
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("分类规则 JSON 无效：") + parseError.errorString();
        return false;
    }
    m_config = document.object();
    m_categories = m_config.value(QStringLiteral("categories")).toObject();
    m_dimensions = m_config.value(QStringLiteral("dimensions")).toObject();
    m_obligationPatterns = m_config.value(QStringLiteral("obligation_patterns")).toObject();
    m_evidenceKeywords = stringArray(m_config.value(QStringLiteral("evidence_keywords")));
    m_taxonomyCategories = QJsonArray();
    m_taxonomyTags = QJsonArray();
    m_taxonomyScenes = QJsonArray();
    // The taxonomy is an optional companion to the legacy rules.json.  Keep
    // the old configuration usable when a user points the application at a
    // custom rules file without the new directory.
    const QString taxonomyPath = taxonomyPathOverride.trimmed().isEmpty()
        ? QFileInfo(configPath).absoluteDir().filePath(QStringLiteral("classification/project_categories.json"))
        : QDir::cleanPath(taxonomyPathOverride);
    QFile taxonomyFile(taxonomyPath);
    if (taxonomyFile.exists() && taxonomyFile.open(QIODevice::ReadOnly)) {
        QJsonParseError taxonomyError;
        const QJsonDocument taxonomyDocument = QJsonDocument::fromJson(taxonomyFile.readAll(), &taxonomyError);
        if (taxonomyError.error == QJsonParseError::NoError && taxonomyDocument.isObject()) {
            const QJsonObject taxonomy = taxonomyDocument.object();
            m_taxonomyCategories = taxonomy.value(QStringLiteral("categories")).toArray();
            m_taxonomyTags = taxonomy.value(QStringLiteral("professional_tags")).toArray();
            m_taxonomyScenes = taxonomy.value(QStringLiteral("scenes")).toArray();
        } else if (error) {
            *error = QStringLiteral("分类目录 JSON 无效：") + taxonomyError.errorString();
            return false;
        }
    }
    if (m_categories.isEmpty()) {
        if (error) *error = QStringLiteral("分类规则中没有 categories 配置");
        return false;
    }
    return true;
}

QStringList RuleEngine::categoryCodes() const
{
    QStringList result = m_categories.keys();
    std::sort(result.begin(), result.end());
    return result;
}

QString RuleEngine::categoryName(const QString& code) const
{
    return m_categories.value(code).toObject().value(QStringLiteral("name")).toString();
}

int RuleEngine::score(const QString& text, const QJsonArray& keywords, QStringList* matched)
{
    QStringList found;
    for (const QJsonValue& value : keywords) {
        const QString keyword = value.toString();
        if (keyword.isEmpty()) continue;
        if (text.contains(keyword) && !found.contains(keyword)) found.append(keyword);
    }
    int total = 0;
    for (const QString& keyword : found) {
        bool coveredBySpecific = false;
        for (const QString& other : found) {
            if (other.size() > keyword.size() && other.contains(keyword)) {
                coveredBySpecific = true;
                break;
            }
        }
        if (!coveredBySpecific) total += keyword.size() >= 4 ? 2 : 1;
    }
    if (matched) matched->append(found);
    return total;
}

QString RuleEngine::titleFor(const QString& text, const QString& article) const
{
    QString title = text.section('\n', 0, 0).trimmed();
    if (!article.isEmpty() && title.startsWith(article)) {
        title = title.mid(article.size()).trimmed();
        while (title.startsWith(QChar(':')) || title.startsWith(QChar(0xFF1A)) || title.startsWith(QChar(0x3001))) {
            title.remove(0, 1);
            title = title.trimmed();
        }
    }
    if (title.size() > 80) title = title.left(80) + QStringLiteral("…");
    return title;
}

QStringList RuleEngine::extractConditions(const QString& text) const
{
    QStringList result;
    const QStringList patterns = {
        QStringLiteral("[^。；;\\n]{0,80}(?:达到一定规模|符合下列条件|必要时|根据工程特点|按照规定)[^。；;\\n]{0,180}[。；;]?"),
        QStringLiteral("(?:^|[，。；;])\\s*(?:当|若|如果)[^。；;\\n]{0,180}[。；;]?")
    };
    for (const QString& pattern : patterns) {
        const QRegularExpression expression(pattern);
        QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
        while (iterator.hasNext()) {
            const QString value = iterator.next().captured(0).trimmed();
            if (!value.isEmpty() && !result.contains(value)) result.append(value);
        }
    }
    return result.mid(0, 8);
}
QStringList RuleEngine::extractEvidence(const QString& text) const
{
    QStringList result;
    for (const QString& keyword : m_evidenceKeywords) {
        if (text.contains(keyword)) result.append(keyword);
    }
    return result;
}

QString RuleEngine::obligationFor(const QString& text) const
{
    const auto containsAny = [&text](const QStringList& words) {
        for (const QString& word : words) {
            if (text.contains(word)) return true;
        }
        return false;
    };
    if (containsAny(stringArray(m_obligationPatterns.value(QStringLiteral("必须"))))) return QStringLiteral("必须/禁止");
    if (containsAny(stringArray(m_obligationPatterns.value(QStringLiteral("应当"))))) return QStringLiteral("应当");
    if (containsAny(stringArray(m_obligationPatterns.value(QStringLiteral("条件性"))))) return QStringLiteral("条件性");
    if (containsAny(stringArray(m_obligationPatterns.value(QStringLiteral("可以"))))) return QStringLiteral("可以/建议");
    return QStringLiteral("未识别");
}

void RuleEngine::classifyTaxonomy(const QString& text, ClauseRecord* record) const
{
    if (!record) return;

    // Subcategories are evaluated only inside the selected P01—P09 classes;
    // this prevents a word such as “道路” from assigning a P09 subtype to a
    // clause that was classified as a water or building rule.
    for (const QJsonValue& value : m_taxonomyCategories) {
        const QJsonObject category = value.toObject();
        const QString categoryCode = category.value(QStringLiteral("code")).toString();
        if (!record->categories.contains(categoryCode)) continue;
        for (const QJsonValue& subValue : category.value(QStringLiteral("subcategories")).toArray()) {
            const QJsonObject subcategory = subValue.toObject();
            const QString code = subcategory.value(QStringLiteral("code")).toString();
            bool matched = false;
            for (const QString& keyword : stringArray(subcategory.value(QStringLiteral("include_keywords")))) {
                if (!keyword.isEmpty() && text.contains(keyword)) {
                    matched = true;
                    record->matchedKeywords.append(keyword);
                }
            }
            if (matched && !record->subcategories.contains(code)) record->subcategories.append(code);
        }
    }

    for (const QJsonValue& value : m_taxonomyTags) {
        const QJsonObject tag = value.toObject();
        bool matched = false;
        for (const QString& keyword : stringArray(tag.value(QStringLiteral("keywords")))) {
            if (!keyword.isEmpty() && text.contains(keyword)) {
                matched = true;
                record->matchedKeywords.append(keyword);
            }
        }
        const QString code = tag.value(QStringLiteral("code")).toString();
        if (matched && !code.isEmpty() && !record->professionalTagCodes.contains(code)) record->professionalTagCodes.append(code);
    }

    for (const QJsonValue& value : m_taxonomyScenes) {
        const QJsonObject scene = value.toObject();
        bool matched = false;
        for (const QString& keyword : stringArray(scene.value(QStringLiteral("keywords")))) {
            if (!keyword.isEmpty() && text.contains(keyword)) {
                matched = true;
                record->matchedKeywords.append(keyword);
            }
        }
        const QString code = scene.value(QStringLiteral("code")).toString();
        if (matched && !code.isEmpty() && !record->sceneCodes.contains(code)) record->sceneCodes.append(code);
    }
    record->subcategories = unique(record->subcategories);
    record->professionalTagCodes = unique(record->professionalTagCodes);
    record->sceneCodes = unique(record->sceneCodes);
    record->matchedKeywords = unique(record->matchedKeywords);
}

ClauseRecord RuleEngine::classify(const QString& sourceFile, int page, const QString& article,
                                  const QString& text, int ordinal) const
{
    ClauseRecord record;
    record.sourceFile = sourceFile;
    const QString sourceStem = QFileInfo(sourceFile).completeBaseName();
    const QRegularExpression documentExpression(
        QStringLiteral("((?:GB(?:\\s*/?\\s*T)?|JGJ(?:\\s*/?\\s*T)?|CJJ(?:\\s*/?\\s*T)?|JTG(?:\\s*/?\\s*T)?|SL(?:\\s*/?\\s*T)?|DL(?:\\s*/?\\s*T)?|DB\\d*|NB(?:\\s*/?\\s*T)?|GBZ|ISO)\\s*[A-Za-z0-9./-]*\\d+[A-Za-z0-9./-]*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch documentMatch = documentExpression.match(sourceStem);
    record.documentCode = documentMatch.hasMatch() ? documentMatch.captured(1).simplified() : sourceStem;
    record.documentTitle = sourceStem;
    record.sourcePage = page;
    record.article = article;
    record.text = text;
    record.title = titleFor(text, article);

    struct CategoryScore {
        QString code;
        int score = 0;
        int coreScore = 0;
        QStringList matches;
        QStringList coreMatches;
        QStringList negativeMatches;
    };
    QList<CategoryScore> categoryScores;
    QJsonObject categoryEvidence;
    for (const QString& code : categoryCodes()) {
        const QJsonObject category = m_categories.value(code).toObject();
        QStringList matches;
        QStringList coreMatches;
        int value = score(text, category.value(QStringLiteral("keywords")).toArray(), &matches);
        const int coreValue = score(text, category.value(QStringLiteral("core_keywords")).toArray(), &coreMatches);
        QStringList negativeMatches;
        for (const QString& negative : stringArray(category.value(QStringLiteral("negative")))) {
            if (text.contains(negative) && !negativeMatches.contains(negative)) {
                negativeMatches.append(negative);
                value -= negative.size() >= 4 ? 2 : 1;
            }
        }
        CategoryScore item{code, qMax(value, 0), coreValue, matches, coreMatches, negativeMatches};
        categoryScores.append(item);
        QJsonObject evidence;
        evidence[QStringLiteral("score")] = item.score;
        evidence[QStringLiteral("core_score")] = item.coreScore;
        const bool eligible = item.coreScore > 0 && (item.score > 0 || item.coreScore >= 2);
        evidence[QStringLiteral("eligible")] = eligible;
        evidence[QStringLiteral("matched_keywords")] = QJsonArray::fromStringList(item.matches);
        evidence[QStringLiteral("core_keywords")] = QJsonArray::fromStringList(item.coreMatches);
        evidence[QStringLiteral("negative_keywords")] = QJsonArray::fromStringList(item.negativeMatches);
        categoryEvidence[code] = evidence;
    }
    std::sort(categoryScores.begin(), categoryScores.end(), [](const CategoryScore& left, const CategoryScore& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.coreScore != right.coreScore) return left.coreScore > right.coreScore;
        return left.code < right.code;
    });
    QList<CategoryScore> eligible;
    for (const CategoryScore& item : categoryScores) {
        const bool usable = item.coreScore > 0 && (item.score > 0 || item.coreScore >= 2);
        if (usable) eligible.append(item);
    }
    if (eligible.isEmpty()) {
        record.categories = {QStringLiteral("待判定")};
        record.categoryConfidence = QStringLiteral("待判定");
        record.classificationStatus = QStringLiteral("needs_review");
    } else {
        for (const CategoryScore& item : eligible) {
            record.categories.append(item.code);
            record.matchedKeywords.append(item.matches);
        }
        if (record.categories.size() == 1) {
            record.classificationStatus = QStringLiteral("classified");
            const int scoreValue = eligible.first().score;
            if (scoreValue >= 6 || eligible.first().coreScore >= 4) record.categoryConfidence = QStringLiteral("高");
            else if (scoreValue >= 2) record.categoryConfidence = QStringLiteral("中");
            else record.categoryConfidence = QStringLiteral("低");
        } else {
            record.classificationStatus = QStringLiteral("multiple_candidates");
            record.categoryConfidence = QStringLiteral("多候选");
        }
    }
    record.categoryEvidence = categoryEvidence;
    for (const QString& code : record.categories) {
        if (m_categories.contains(code)) record.categoryNames.append(categoryName(code));
    }

    QMap<QString, int> dimensionScores;
    QMap<QString, QStringList> dimensionMatches;
    for (const QString& dimension : m_dimensions.keys()) {
        QStringList matches;
        const int value = score(text, m_dimensions.value(dimension).toArray(), &matches);
        if (value > 0) {
            dimensionScores.insert(dimension, value);
            dimensionMatches.insert(dimension, matches);
        }
    }
    QStringList sortedDimensions = dimensionScores.keys();
    std::sort(sortedDimensions.begin(), sortedDimensions.end(), [&dimensionScores](const QString& left, const QString& right) {
        if (dimensionScores[left] != dimensionScores[right]) return dimensionScores[left] > dimensionScores[right];
        return left < right;
    });
    record.dimensions = sortedDimensions;
    for (const QString& dimension : sortedDimensions) record.matchedKeywords.append(dimensionMatches[dimension]);
    record.matchedKeywords = unique(record.matchedKeywords);
    const QMap<QString, QString> dimensionCodes = {
        {QStringLiteral("施工组织"), QStringLiteral("construction")},
        {QStringLiteral("质量"), QStringLiteral("quality")},
        {QStringLiteral("安全"), QStringLiteral("safety")},
        {QStringLiteral("进度"), QStringLiteral("schedule")},
        {QStringLiteral("资源"), QStringLiteral("resources")},
        {QStringLiteral("环境"), QStringLiteral("environment")},
        {QStringLiteral("合同招投标"), QStringLiteral("contract")}
    };
    for (const QString& dimension : record.dimensions) {
        const QString code = dimensionCodes.value(dimension);
        if (!code.isEmpty()) record.dimensionCodes.append(code);
    }
    classifyTaxonomy(text, &record);
    record.obligationLevel = obligationFor(text);
    record.conditions = extractConditions(text);
    record.evidence = extractEvidence(text);

    if (record.classificationStatus == QStringLiteral("needs_review")) {
        record.reviewFlags.append(QStringLiteral("未识别九大类主类，需结合主要建设对象人工判定"));
    }
    if (record.classificationStatus == QStringLiteral("multiple_candidates")) {
        record.reviewFlags.append(QStringLiteral("条款同时命中多个九大类，需确认项目主类或规则适用范围"));
    }
    if (record.categoryConfidence == QStringLiteral("低") || record.categoryConfidence == QStringLiteral("中")) {
        record.reviewFlags.append(QStringLiteral("主类置信度不高，建议人工复核"));
    }
    if (record.categories.size() > 1 &&
        (text.contains(QStringLiteral("不适用于")) || text.contains(QStringLiteral("不属于"))
         || text.contains(QStringLiteral("不包括")) || text.contains(QStringLiteral("除外")))) {
        record.reviewFlags.append(QStringLiteral("存在适用范围或排除范围冲突，需人工复核"));
    }
    if (record.dimensions.isEmpty()) record.reviewFlags.append(QStringLiteral("未识别施工/质量/安全等规则维度"));
    if (record.obligationLevel == QStringLiteral("未识别")) record.reviewFlags.append(QStringLiteral("未识别明确义务强度"));
    if (record.evidence.isEmpty()) record.reviewFlags.append(QStringLiteral("未识别可核查证据词"));
    if (text.size() < 40) record.reviewFlags.append(QStringLiteral("条款文本较短，可能是标题或提取不完整"));
    record.priority = (record.obligationLevel == QStringLiteral("必须/禁止") &&
                       (record.dimensions.contains(QStringLiteral("安全")) || record.dimensions.contains(QStringLiteral("质量"))))
            ? QStringLiteral("高") : (record.dimensions.isEmpty() ? QStringLiteral("低") : QStringLiteral("中"));

    const QByteArray key = sourceFile.toUtf8() + QByteArray::number(page) + article.toUtf8() + QByteArray::number(ordinal) + text.toUtf8();
    record.ruleId = QStringLiteral("R-") + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(10)).toUpper();
    return record;
}




