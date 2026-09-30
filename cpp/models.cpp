#include "models.h"

#include <QJsonArray>

static QJsonArray toArray(const QStringList& values)
{
    QJsonArray result;
    for (const QString& value : values) {
        result.append(value);
    }
    return result;
}

QJsonObject ClauseRecord::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("rule_id")] = ruleId;
    object[QStringLiteral("source_file")] = sourceFile;
    object[QStringLiteral("document_code")] = documentCode;
    object[QStringLiteral("document_title")] = documentTitle;
    object[QStringLiteral("issued_by")] = issuedBy;
    object[QStringLiteral("document_level")] = documentLevel;
    object[QStringLiteral("source_page")] = sourcePage;
    object[QStringLiteral("source_page_end")] = sourcePageEnd;
    object[QStringLiteral("article")] = article;
    object[QStringLiteral("text")] = text;
    object[QStringLiteral("rule_title")] = title;
    object[QStringLiteral("applicable_categories")] = toArray(categories);
    object[QStringLiteral("category_names")] = toArray(categoryNames);
    object[QStringLiteral("subcategory_codes")] = toArray(subcategories);
    object[QStringLiteral("professional_tag_codes")] = toArray(professionalTagCodes);
    object[QStringLiteral("scene_codes")] = toArray(sceneCodes);
    object[QStringLiteral("category_confidence")] = categoryConfidence;
    object[QStringLiteral("classification_status")] = classificationStatus;
    object[QStringLiteral("category_evidence")] = categoryEvidence;
    object[QStringLiteral("dimensions")] = toArray(dimensions);
    object[QStringLiteral("dimension_codes")] = toArray(dimensionCodes);
    object[QStringLiteral("obligation_level")] = obligationLevel;
    object[QStringLiteral("conditions")] = toArray(conditions);
    object[QStringLiteral("evidence")] = toArray(evidence);
    object[QStringLiteral("matched_keywords")] = toArray(matchedKeywords);
    object[QStringLiteral("review_flags")] = toArray(reviewFlags);
    object[QStringLiteral("priority")] = priority;
    return object;
}
