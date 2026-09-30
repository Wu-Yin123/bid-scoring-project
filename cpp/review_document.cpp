#include "review_document.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool isCategory(const QString& value)
{
    return value.size() == 3 && value.startsWith(QStringLiteral("P0"))
        && value.at(2) >= QLatin1Char('1') && value.at(2) <= QLatin1Char('9');
}

QString resolvedPath(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists()) return info.canonicalFilePath();
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    return parent.isEmpty() ? QString() : QDir(parent).filePath(info.fileName());
}
}

bool ReviewDocument::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("无法打开结果文件：") + file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("结果文件不是有效 JSON 对象：") + parseError.errorString());
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("rules")).isArray() || !root.value(QStringLiteral("metadata")).isObject())
        return fail(error, QStringLiteral("请选择程序生成的 rules.json 或复核结果文件，需包含 metadata 和 rules。"));
    const QJsonArray rules = root.value(QStringLiteral("rules")).toArray();
    for (int index = 0; index < rules.size(); ++index) {
        const auto value = rules.at(index);
        const auto record = value.toObject();
        if (!value.isObject() || !record.value(QStringLiteral("text")).isString()
            || record.value(QStringLiteral("rule_id")).toString().isEmpty())
            return fail(error, QStringLiteral("第 %1 条记录缺少有效的原文或规则编号。").arg(index + 1));
        if (record.contains(QStringLiteral("human_review"))) {
            const auto review = record.value(QStringLiteral("human_review")).toObject();
            const QString status = review.value(QStringLiteral("status")).toString();
            const auto categories = review.value(QStringLiteral("applicable_categories"));
            if ((status != QStringLiteral("pending") && status != QStringLiteral("confirmed"))
                || !categories.isArray() || !review.value(QStringLiteral("note")).isString()
                || (status == QStringLiteral("confirmed") && categories.toArray().isEmpty()))
                return fail(error, QStringLiteral("第 %1 条记录的人工复核格式无效。").arg(index + 1));
            for (const auto category : categories.toArray()) {
                if (!isCategory(category.toString()))
                    return fail(error, QStringLiteral("第 %1 条人工复核包含九大类以外的类别。").arg(index + 1));
            }
        }
    }
    const auto reviewMeta = root.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("human_review")).toObject();
    if (!reviewMeta.isEmpty() && (reviewMeta.value(QStringLiteral("schema_version")).toInt() != 1
        || reviewMeta.value(QStringLiteral("original_result_path")).toString().isEmpty()))
        return fail(error, QStringLiteral("不支持此人工复核文件版本或缺少原始结果路径。"));

    m_root = root;
    m_rules = rules;
    m_sourcePath = QFileInfo(path).absoluteFilePath();
    m_originalPath = reviewMeta.isEmpty() ? m_sourcePath : reviewMeta.value(QStringLiteral("original_result_path")).toString();
    m_savePath = reviewMeta.isEmpty() ? QString() : m_sourcePath;
    m_dirty = false;
    return true;
}

QJsonObject ReviewDocument::categoryNames() const
{
    QJsonObject names = m_root.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("category_names")).toObject();
    const QStringList defaults = {
        QStringLiteral("房屋建筑新建及扩建工程"), QStringLiteral("建筑装饰、修缮及既有建筑改造工程"),
        QStringLiteral("市政道路、桥梁及交通工程"), QStringLiteral("市政管网、排水及防涝工程"),
        QStringLiteral("水利工程"), QStringLiteral("高标准农田及土地整治工程"),
        QStringLiteral("乡村建设及农村人居环境整治工程"), QStringLiteral("综合设施提升工程"), QStringLiteral("公路工程")
    };
    for (int i = 0; i < defaults.size(); ++i) {
        const QString id = QStringLiteral("P0%1").arg(i + 1);
        if (names.value(id).toString().isEmpty()) names[id] = defaults.at(i);
    }
    return names;
}

bool ReviewDocument::setReview(int index, const QStringList& categories, const QString& note,
                               bool confirmed, QString* error)
{
    if (index < 0 || index >= count()) return fail(error, QStringLiteral("未选择有效条款。"));
    QStringList selected = categories;
    selected.removeDuplicates();
    selected.sort();
    for (const QString& category : selected) {
        if (!isCategory(category)) return fail(error, QStringLiteral("适用类别只能选择 P01—P09。"));
    }
    if (confirmed && selected.isEmpty()) return fail(error, QStringLiteral("请至少选择一个适用类别；无法判断时保留待复核状态。"));
    auto record = rule(index);
    auto review = record.value(QStringLiteral("human_review")).toObject();
    const auto names = categoryNames();
    QJsonArray selectedNames;
    for (const auto& category : selected) selectedNames.append(names.value(category));
    const QString newStatus = confirmed ? QStringLiteral("confirmed") : QStringLiteral("pending");
    if (review.isEmpty() && newStatus == QStringLiteral("pending") && selected.isEmpty() && note.isEmpty())
        return true;
    if (review.value(QStringLiteral("status")).toString() == newStatus
        && review.value(QStringLiteral("applicable_categories")).toArray() == QJsonArray::fromStringList(selected)
        && review.value(QStringLiteral("note")).toString() == note)
        return true;
    review[QStringLiteral("status")] = newStatus;
    review[QStringLiteral("applicable_categories")] = QJsonArray::fromStringList(selected);
    review[QStringLiteral("category_names")] = selectedNames;
    review[QStringLiteral("note")] = note;
    review[QStringLiteral("updated_at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record[QStringLiteral("human_review")] = review;
    m_rules[index] = record;
    m_dirty = true;
    return true;
}

int ReviewDocument::confirmedCount() const
{
    int result = 0;
    for (const auto value : m_rules) {
        if (value.toObject().value(QStringLiteral("human_review")).toObject().value(QStringLiteral("status")).toString() == QStringLiteral("confirmed")) ++result;
    }
    return result;
}

QJsonObject ReviewDocument::json() const
{
    auto result = m_root;
    result[QStringLiteral("rules")] = m_rules;
    return result;
}

QString ReviewDocument::suggestedSavePath() const
{
    if (!m_savePath.isEmpty()) return m_savePath;
    const QFileInfo source(m_sourcePath);
    const QString parent = source.absolutePath().startsWith(QStringLiteral("D:/Code/Codex/"), Qt::CaseInsensitive)
        ? source.absolutePath() : QStringLiteral("D:/Code/Codex");
    return QDir(parent).filePath(source.completeBaseName() + QStringLiteral(".reviewed.json"));
}

bool ReviewDocument::save(const QString& path, QString* error)
{
    if (m_sourcePath.isEmpty()) return fail(error, QStringLiteral("请先打开处理结果。"));
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString resolved = resolvedPath(absolute);
    const QString allowedRoot = QStringLiteral("D:/Code/Codex/");
    if (!absolute.startsWith(allowedRoot, Qt::CaseInsensitive) || !resolved.startsWith(allowedRoot, Qt::CaseInsensitive))
        return fail(error, QStringLiteral("复核结果必须保存到 D:\\Code\\Codex 内已有的文件夹。"));
    if (absolute.compare(QDir::cleanPath(QFileInfo(m_originalPath).absoluteFilePath()), Qt::CaseInsensitive) == 0
        || resolved.compare(resolvedPath(m_originalPath), Qt::CaseInsensitive) == 0)
        return fail(error, QStringLiteral("请另存为复核文件，不能覆盖原始自动处理结果。"));
    if (!absolute.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        return fail(error, QStringLiteral("复核结果请保存为 .json 文件。"));
    auto output = json();
    auto metadata = output.value(QStringLiteral("metadata")).toObject();
    auto reviewMeta = metadata.value(QStringLiteral("human_review")).toObject();
    reviewMeta[QStringLiteral("schema_version")] = 1;
    reviewMeta[QStringLiteral("original_result_path")] = m_originalPath;
    reviewMeta[QStringLiteral("saved_at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    reviewMeta[QStringLiteral("confirmed_count")] = confirmedCount();
    reviewMeta[QStringLiteral("pending_count")] = count() - confirmedCount();
    metadata[QStringLiteral("human_review")] = reviewMeta;
    output[QStringLiteral("metadata")] = metadata;
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, QStringLiteral("无法保存：") + file.errorString());
    const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(error, QStringLiteral("保存未完成：") + file.errorString());
    m_root = output;
    m_savePath = absolute;
    m_dirty = false;
    return true;
}
