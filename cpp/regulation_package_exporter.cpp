#include "regulation_package_exporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {

QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text)) result.append(text);
    }
    return result;
}

QJsonArray array(const QStringList& values)
{
    QJsonArray result;
    QSet<QString> seen;
    for (const QString& value : values) {
        const QString text = value.trimmed();
        if (text.isEmpty() || seen.contains(text)) continue;
        seen.insert(text);
        result.append(text);
    }
    return result;
}

void appendUnique(QStringList* target, const QStringList& values)
{
    if (!target) return;
    for (const QString& value : values) {
        const QString text = value.trimmed();
        if (!text.isEmpty() && !target->contains(text)) target->append(text);
    }
}

QString jsonString(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString().trimmed();
}

bool readJson(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开 JSON：") + path;
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("JSON 无效：") + path + QStringLiteral("；") + parseError.errorString();
        return false;
    }
    *object = document.object();
    return true;
}

bool writeJson(const QString& path, const QJsonObject& object, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入 JSON：") + path + QStringLiteral("；") + file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("无法提交 JSON：") + path + QStringLiteral("；") + file.errorString();
        return false;
    }
    return true;
}

bool writeText(const QString& path, const QString& text, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入文本：") + path + QStringLiteral("；") + file.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("无法提交文本：") + path + QStringLiteral("；") + file.errorString();
        return false;
    }
    return true;
}

bool copyFile(const QString& source, const QString& destination, QString* error)
{
    if (!QFileInfo::exists(source)) {
        if (error) *error = QStringLiteral("源文件不存在：") + source;
        return false;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    if (QFileInfo::exists(destination)) QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        if (error) *error = QStringLiteral("复制文件失败：") + source + QStringLiteral(" -> ") + destination;
        return false;
    }
    return true;
}

QString safeFileStem(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    value = value.trimmed();
    if (value.isEmpty()) value = QStringLiteral("document");
    return value.left(80);
}

QString documentCode(const QJsonObject& rule)
{
    const QString explicitCode = jsonString(rule, QStringLiteral("document_code"));
    if (!explicitCode.isEmpty()) return explicitCode;
    const QString standardCode = jsonString(rule, QStringLiteral("standard_code"));
    if (!standardCode.isEmpty()) return standardCode;
    const QString source = QFileInfo(jsonString(rule, QStringLiteral("source_file"))).completeBaseName();
    // Covers common Chinese construction identifiers such as GB50026-2020,
    // GB/T 50326-2017, JGJ/T 46-2005, CJJ 1-2008 and SL/T 225-98.
    const QRegularExpression expression(
        QStringLiteral("((?:GB(?:\\s*/?\\s*T)?|JGJ(?:\\s*/?\\s*T)?|CJJ(?:\\s*/?\\s*T)?|JTG(?:\\s*/?\\s*T)?|SL(?:\\s*/?\\s*T)?|DL(?:\\s*/?\\s*T)?|DB\\d*|NB(?:\\s*/?\\s*T)?|GBZ|ISO)\\s*[A-Za-z0-9./-]*\\d+[A-Za-z0-9./-]*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(source);
    if (match.hasMatch()) return match.captured(1).simplified();
    return QStringLiteral("DOC-") + QString::fromLatin1(
        QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha1).toHex().left(12)).toUpper();
}

QString documentTitle(const QJsonObject& rule, const QString& code)
{
    const QString explicitTitle = jsonString(rule, QStringLiteral("document_title"));
    if (!explicitTitle.isEmpty()) return explicitTitle;
    QString title = QFileInfo(jsonString(rule, QStringLiteral("source_file"))).completeBaseName();
    if (title.isEmpty()) title = code;
    return title;
}

QString inferredLevel(const QString& code, const QJsonObject& rule)
{
    const QString explicitLevel = jsonString(rule, QStringLiteral("document_level"));
    if (!explicitLevel.isEmpty()) return explicitLevel;
    if (code.startsWith(QStringLiteral("GB"), Qt::CaseInsensitive)) return QStringLiteral("national_standard");
    if (code.startsWith(QStringLiteral("JGJ"), Qt::CaseInsensitive)
        || code.startsWith(QStringLiteral("CJJ"), Qt::CaseInsensitive)
        || code.startsWith(QStringLiteral("JTG"), Qt::CaseInsensitive)
        || code.startsWith(QStringLiteral("SL"), Qt::CaseInsensitive)
        || code.startsWith(QStringLiteral("DL"), Qt::CaseInsensitive)) return QStringLiteral("industry_standard");
    const QString source = jsonString(rule, QStringLiteral("source_file"));
    if (source.contains(QStringLiteral("中华人民共和国")) && source.contains(QChar(0x6CD5)))
        return QStringLiteral("law");
    if (source.contains(QChar(0x6CD5)) || source.contains(QStringLiteral("条例"))
        || source.contains(QStringLiteral("办法")) || source.contains(QStringLiteral("规定")))
        return QStringLiteral("law_or_regulation");
    if (source.contains(QStringLiteral("标准")) || source.contains(QStringLiteral("规范"))
        || source.contains(QStringLiteral("规程")) || source.contains(QStringLiteral("细则")))
        return QStringLiteral("standard");
    return QStringLiteral("unknown");
}

QString inferredIssuer(const QJsonObject& rule)
{
    const QString explicitIssuer = jsonString(rule, QStringLiteral("issued_by"));
    if (!explicitIssuer.isEmpty()) return explicitIssuer;
    const QString source = jsonString(rule, QStringLiteral("source_file"));
    if (source.contains(QStringLiteral("住房和城乡建设部"))) return QStringLiteral("住房和城乡建设部");
    if (source.contains(QStringLiteral("交通运输部"))) return QStringLiteral("交通运输部");
    if (source.contains(QStringLiteral("水利部"))) return QStringLiteral("水利部");
    if (source.contains(QStringLiteral("国务院"))) return QStringLiteral("国务院");
    if (source.contains(QStringLiteral("中华人民共和国")) && source.contains(QChar(0x6CD5)))
        return QStringLiteral("全国人民代表大会常务委员会");
    return QString();
}

QString projectTypeFor(const QString& category)
{
    static const QMap<QString, QString> mapping = {
        {QStringLiteral("P01"), QStringLiteral("building_construction")},
        {QStringLiteral("P02"), QStringLiteral("municipal_renovation")},
        {QStringLiteral("P03"), QStringLiteral("municipal_road")},
        {QStringLiteral("P04"), QStringLiteral("municipal_pipe_network")},
        {QStringLiteral("P05"), QStringLiteral("water_conservancy")},
        {QStringLiteral("P06"), QStringLiteral("high_standard_farmland")},
        {QStringLiteral("P07"), QStringLiteral("large_natural_village_renovation")},
        // These two packages are emitted for the future ProjectProfile route;
        // the current ZIP loader does not yet list them as legacy project types.
        {QStringLiteral("P08"), QStringLiteral("integrated_facilities")},
        {QStringLiteral("P09"), QStringLiteral("highway")}
    };
    return mapping.value(category);
}

QStringList itemCodes(const QStringList& dimensions)
{
    QStringList result;
    static const QMap<QString, QStringList> mapping = {
        {QStringLiteral("施工组织"), {QStringLiteral("ITEM-01"), QStringLiteral("ITEM-02"), QStringLiteral("ITEM-10")}},
        {QStringLiteral("质量"), {QStringLiteral("ITEM-06")}},
        {QStringLiteral("安全"), {QStringLiteral("ITEM-07")}},
        {QStringLiteral("进度"), {QStringLiteral("ITEM-08")}},
        {QStringLiteral("资源"), {QStringLiteral("ITEM-03"), QStringLiteral("ITEM-04"), QStringLiteral("ITEM-05")}},
        {QStringLiteral("环境"), {QStringLiteral("ITEM-09")}}
    };
    for (const QString& dimension : dimensions) appendUnique(&result, mapping.value(dimension));
    return result;
}

QString statusFor(const QJsonObject& rule)
{
    const QString humanStatus = jsonString(rule.value(QStringLiteral("human_review")).toObject(), QStringLiteral("status"));
    if (!humanStatus.isEmpty()) return humanStatus;
    const QString status = jsonString(rule, QStringLiteral("status"));
    return status.isEmpty() ? QStringLiteral("pending_review") : status;
}

QStringList categoriesFor(const QJsonObject& rule)
{
    QStringList result = strings(rule.value(QStringLiteral("categories")));
    appendUnique(&result, strings(rule.value(QStringLiteral("applicable_categories"))));
    result.removeAll(QStringLiteral("待判定"));
    return result;
}

QStringList fieldStrings(const QJsonObject& rule, const QString& primary, const QString& fallback = QString())
{
    QStringList result = strings(rule.value(primary));
    if (!fallback.isEmpty()) appendUnique(&result, strings(rule.value(fallback)));
    return result;
}

void mergeArrayField(QJsonObject* target, const QString& key, const QStringList& values)
{
    if (!target) return;
    QStringList merged = strings(target->value(key));
    appendUnique(&merged, values);
    (*target)[key] = array(merged);
}

void mergeTextField(QJsonObject* target, const QString& key, const QString& value, const QString& separator)
{
    if (!target || value.trimmed().isEmpty()) return;
    const QString existing = target->value(key).toString();
    if (existing.isEmpty()) (*target)[key] = value.trimmed();
    else if (!existing.contains(value.trimmed())) (*target)[key] = existing + separator + value.trimmed();
}

QJsonObject runtimeEntry(const QJsonObject& rule)
{
    const QString code = documentCode(rule);
    const QString title = documentTitle(rule, code);
    const QStringList dimensions = fieldStrings(rule, QStringLiteral("dimensions"));
    const QStringList categories = categoriesFor(rule);
    const QStringList subcategories = fieldStrings(rule, QStringLiteral("subcategory_codes"), QStringLiteral("subcategories"));
    const QStringList tags = fieldStrings(rule, QStringLiteral("professional_tag_codes"));
    const QStringList scenes = fieldStrings(rule, QStringLiteral("scene_codes"));
    QStringList keywords = fieldStrings(rule, QStringLiteral("matched_keywords"));
    appendUnique(&keywords, {code, title});
    appendUnique(&keywords, categories);
    appendUnique(&keywords, subcategories);

    QJsonObject entry;
    // These eleven fields are the stable contract read by the future Python
    // scoring project.  The additional fields below are deliberately retained
    // for ProjectProfile routing and traceability.
    entry[QStringLiteral("code")] = code;
    entry[QStringLiteral("title")] = title;
    entry[QStringLiteral("issued_by")] = inferredIssuer(rule);
    entry[QStringLiteral("level")] = inferredLevel(code, rule);
    entry[QStringLiteral("is_mandatory")] = rule.value(QStringLiteral("is_mandatory")).toBool(false);
    entry[QStringLiteral("applies_to_items")] = array(itemCodes(dimensions));
    QStringList factors = categories;
    appendUnique(&factors, subcategories);
    appendUnique(&factors, tags);
    entry[QStringLiteral("applies_to_factors")] = array(factors);
    entry[QStringLiteral("keywords")] = array(keywords);
    QString scope = jsonString(rule, QStringLiteral("scope_clause"));
    if (scope.isEmpty()) scope = jsonString(rule, QStringLiteral("article"));
    entry[QStringLiteral("scope_clause")] = scope;
    entry[QStringLiteral("requirement_text")] = jsonString(rule, QStringLiteral("text"));
    const QString action = jsonString(rule, QStringLiteral("non_comply_action"));
    entry[QStringLiteral("non_comply_action")] = (action == QStringLiteral("cap_to_general")
        || action == QStringLiteral("cap_to_negative") || action == QStringLiteral("warning_only"))
        ? action : QStringLiteral("warning_only");

    entry[QStringLiteral("standard_code")] = code;
    entry[QStringLiteral("source_rule_ids")] = QJsonArray{jsonString(rule, QStringLiteral("rule_id"))};
    entry[QStringLiteral("source_files")] = QJsonArray{jsonString(rule, QStringLiteral("source_file"))};
    entry[QStringLiteral("category_codes")] = array(categories);
    entry[QStringLiteral("subcategory_codes")] = array(subcategories);
    entry[QStringLiteral("professional_tag_codes")] = array(tags);
    entry[QStringLiteral("scene_codes")] = array(scenes);
    entry[QStringLiteral("dimensions")] = array(dimensions);
    entry[QStringLiteral("review_status")] = statusFor(rule);
    entry[QStringLiteral("pending_review")] = statusFor(rule) != QStringLiteral("confirmed");
    entry[QStringLiteral("clause_count")] = 1;
    return entry;
}

void mergeRuntimeEntry(QJsonObject* target, const QJsonObject& incoming)
{
    if (!target) return;
    const QStringList arrayKeys = {
        QStringLiteral("applies_to_items"), QStringLiteral("applies_to_factors"),
        QStringLiteral("keywords"), QStringLiteral("source_rule_ids"), QStringLiteral("source_files"),
        QStringLiteral("category_codes"), QStringLiteral("subcategory_codes"),
        QStringLiteral("professional_tag_codes"), QStringLiteral("scene_codes"), QStringLiteral("dimensions")
    };
    for (const QString& key : arrayKeys) mergeArrayField(target, key, strings(incoming.value(key)));
    mergeTextField(target, QStringLiteral("scope_clause"), incoming.value(QStringLiteral("scope_clause")).toString(), QStringLiteral("；"));
    mergeTextField(target, QStringLiteral("requirement_text"), incoming.value(QStringLiteral("requirement_text")).toString(), QStringLiteral("\n"));
    const int count = target->value(QStringLiteral("clause_count")).toInt(0) + incoming.value(QStringLiteral("clause_count")).toInt(1);
    (*target)[QStringLiteral("clause_count")] = count;
    if (incoming.value(QStringLiteral("review_status")).toString() != QStringLiteral("confirmed")) {
        (*target)[QStringLiteral("pending_review")] = true;
    }
}

bool validateRuntimeEntry(const QJsonObject& entry, QString* error)
{
    const QStringList required = {
        QStringLiteral("code"), QStringLiteral("title"), QStringLiteral("issued_by"),
        QStringLiteral("level"), QStringLiteral("is_mandatory"), QStringLiteral("applies_to_items"),
        QStringLiteral("applies_to_factors"), QStringLiteral("keywords"), QStringLiteral("scope_clause"),
        QStringLiteral("requirement_text"), QStringLiteral("non_comply_action")
    };
    for (const QString& key : required) {
        if (!entry.contains(key)) {
            if (error) *error = QStringLiteral("运行包条目缺少字段：") + key;
            return false;
        }
    }
    const QString action = entry.value(QStringLiteral("non_comply_action")).toString();
    if (action != QStringLiteral("cap_to_general") && action != QStringLiteral("cap_to_negative")
        && action != QStringLiteral("warning_only")) {
        if (error) *error = QStringLiteral("运行包条目的 non_comply_action 无效：") + action;
        return false;
    }
    if (entry.value(QStringLiteral("code")).toString().trimmed().isEmpty()
        || !entry.value(QStringLiteral("applies_to_items")).isArray()
        || !entry.value(QStringLiteral("applies_to_factors")).isArray()
        || !entry.value(QStringLiteral("keywords")).isArray()) {
        if (error) *error = QStringLiteral("运行包条目的字段类型无效：") + entry.value(QStringLiteral("code")).toString();
        return false;
    }
    return true;
}

} // namespace

RegulationExportResult RegulationPackageExporter::exportPackage(const RegulationExportOptions& options)
{
    RegulationExportResult result;
    result.packageRoot = QDir::cleanPath(QFileInfo(options.outputRoot).absoluteFilePath());
    result.manifestPath = QDir(result.packageRoot).filePath(QStringLiteral("package_manifest.json"));
    QString normalizedPackageRoot = result.packageRoot;
    normalizedPackageRoot.replace(QChar('\\'), QChar('/'));
    const QString allowedRoot = QStringLiteral("D:/Code/Codex");
    if (normalizedPackageRoot.compare(allowedRoot, Qt::CaseInsensitive) != 0
        && !normalizedPackageRoot.startsWith(allowedRoot + QChar('/'), Qt::CaseInsensitive)) {
        result.errors.append(QStringLiteral("运行配置包路径必须位于 D:\\Code\\Codex 下：") + result.packageRoot);
        return result;
    }

    QJsonObject master;
    QString error;
    if (!readJson(options.masterPath, &master, &error)) {
        result.errors.append(error);
        return result;
    }
    const QJsonArray rules = master.value(QStringLiteral("rules")).toArray();
    if (rules.isEmpty()) result.warnings.append(QStringLiteral("主规则库当前没有 rules，仍会生成空的兼容包。"));

    QDir root(result.packageRoot);
    if (!root.mkpath(QStringLiteral("config/regulations")) || !root.mkpath(QStringLiteral("config/classification"))
        || !root.mkpath(QStringLiteral("config/mappings")) || !root.mkpath(QStringLiteral("data"))) {
        result.errors.append(QStringLiteral("无法创建运行配置包目录：") + result.packageRoot);
        return result;
    }

    QMap<QString, QMap<QString, QJsonObject>> packageEntries;
    packageEntries.insert(QStringLiteral("common"), {});
    int unclassifiedCount = 0;
    QStringList unclassifiedSamples;
    for (const QJsonValue& value : rules) {
        const QJsonObject rule = value.toObject();
        if (rule.isEmpty()) { ++result.skippedRules; continue; }
        const QString status = statusFor(rule);
        if (status == QStringLiteral("deprecated") && !options.includeDeprecated) { ++result.skippedRules; continue; }
        if (!options.includePending && status != QStringLiteral("confirmed")) { ++result.skippedRules; continue; }
        const QString code = documentCode(rule);
        if (code.isEmpty()) { ++result.skippedRules; result.warnings.append(QStringLiteral("跳过没有文档编号的规则。")); continue; }
        const QJsonObject entry = runtimeEntry(rule);
        auto addToPackage = [&](const QString& package) {
            if (package.isEmpty()) return;
            if (!packageEntries.contains(package)) packageEntries.insert(package, {});
            auto map = packageEntries.value(package);
            if (map.contains(code)) mergeRuntimeEntry(&map[code], entry);
            else map.insert(code, entry);
            packageEntries[package] = map;
        };
        addToPackage(QStringLiteral("common"));
        const QStringList categories = categoriesFor(rule);
        if (categories.isEmpty()) {
            ++unclassifiedCount;
            if (unclassifiedSamples.size() < 8) unclassifiedSamples.append(jsonString(rule, QStringLiteral("rule_id")));
        } else {
            for (const QString& category : categories) addToPackage(projectTypeFor(category));
        }
        ++result.exportedRules;
        if (status != QStringLiteral("confirmed")) ++result.skippedRules; // count pending for UI transparency
    }
    if (unclassifiedCount > 0) {
        result.warnings.append(QStringLiteral("%1 条规则未确认 P01-P09，已只放入 common 包；示例：%2")
                               .arg(unclassifiedCount).arg(unclassifiedSamples.join(QStringLiteral("、"))));
    }

    for (auto package = packageEntries.cbegin(); package != packageEntries.cend(); ++package) {
        QJsonArray regulations;
        for (auto entry = package.value().cbegin(); entry != package.value().cend(); ++entry) regulations.append(entry.value());
        result.runtimeEntries += regulations.size();
        for (const QJsonValue& value : regulations) {
            QString validationError;
            if (!validateRuntimeEntry(value.toObject(), &validationError)) result.errors.append(package.key() + QStringLiteral("：") + validationError);
        }
        QJsonObject rootObject;
        rootObject[QStringLiteral("regulations")] = regulations;
        rootObject[QStringLiteral("schema_version")] = QStringLiteral("regulation-runtime.v1");
        rootObject[QStringLiteral("package_name")] = package.key();
        const QString filePath = root.filePath(QStringLiteral("config/regulations/") + package.key() + QStringLiteral(".json"));
        if (!writeJson(filePath, rootObject, &error)) result.errors.append(error);
        else result.generatedFiles.append(filePath);
    }

    if (!options.taxonomyPath.isEmpty()) {
        const QString destination = root.filePath(QStringLiteral("config/classification/project_categories.json"));
        if (!copyFile(options.taxonomyPath, destination, &error)) result.errors.append(error);
        else result.generatedFiles.append(destination);
        const QString profilesSource = QDir(QFileInfo(options.taxonomyPath).absoluteDir()).filePath(QStringLiteral("project_profiles.json"));
        const QString profilesDestination = root.filePath(QStringLiteral("config/classification/project_profiles.json"));
        if (QFileInfo::exists(profilesSource)) {
            if (!copyFile(profilesSource, profilesDestination, &error)) result.errors.append(error);
            else result.generatedFiles.append(profilesDestination);
        }
    } else {
        result.warnings.append(QStringLiteral("未指定分类目录，运行包未复制 project_categories.json。"));
    }

    // Carry the machine-readable contracts into the package so the future
    // importer can validate a package without depending on the Qt project.
    const QString configRoot = options.taxonomyPath.isEmpty()
        ? QString()
        : QFileInfo(options.taxonomyPath).absoluteDir().absolutePath();
    const QStringList schemaNames = {
        QStringLiteral("regulation_runtime.schema.json"),
        QStringLiteral("regulation_master.schema.json"),
        QStringLiteral("project_profile.schema.json")
    };
    for (const QString& schemaName : schemaNames) {
        const QString source = QDir(configRoot).filePath(QStringLiteral("../regulation_schema/") + schemaName);
        const QString destination = root.filePath(QStringLiteral("config/regulation_schema/") + schemaName);
        if (QFileInfo::exists(source)) {
            if (!copyFile(source, destination, &error)) result.errors.append(error);
            else result.generatedFiles.append(destination);
        }
    }
    const QString dimensionMapping = QDir(configRoot).filePath(QStringLiteral("../mappings/dimension_to_runtime_items.json"));
    const QString mappingDestination = root.filePath(QStringLiteral("config/mappings/dimension_to_runtime_items.json"));
    if (QFileInfo::exists(dimensionMapping)) {
        if (!copyFile(dimensionMapping, mappingDestination, &error)) result.errors.append(error);
        else result.generatedFiles.append(mappingDestination);
    }

    QJsonObject projectMap;
    projectMap[QStringLiteral("schema_version")] = QStringLiteral("project-type-map.v1");
    projectMap[QStringLiteral("P01")] = QStringLiteral("building_construction");
    projectMap[QStringLiteral("P02")] = QStringLiteral("municipal_renovation");
    projectMap[QStringLiteral("P03")] = QStringLiteral("municipal_road");
    projectMap[QStringLiteral("P04")] = QStringLiteral("municipal_pipe_network");
    projectMap[QStringLiteral("P05")] = QStringLiteral("water_conservancy");
    projectMap[QStringLiteral("P06")] = QStringLiteral("high_standard_farmland");
    projectMap[QStringLiteral("P07")] = QStringLiteral("large_natural_village_renovation");
    projectMap[QStringLiteral("P08")] = QStringLiteral("integrated_facilities");
    projectMap[QStringLiteral("P09")] = QStringLiteral("highway");
    projectMap[QStringLiteral("supported_legacy_project_types")] = QJsonArray{
        QStringLiteral("municipal_road"), QStringLiteral("municipal_renovation"),
        QStringLiteral("large_natural_village_renovation"), QStringLiteral("building_construction"),
        QStringLiteral("municipal_pipe_network"), QStringLiteral("municipal_green"),
        QStringLiteral("water_conservancy"), QStringLiteral("high_standard_farmland")
    };
    projectMap[QStringLiteral("package_files")] = QJsonObject{
        {QStringLiteral("municipal_road"), QStringLiteral("config/regulations/municipal_road.json")},
        {QStringLiteral("municipal_renovation"), QStringLiteral("config/regulations/municipal_renovation.json")},
        {QStringLiteral("large_natural_village_renovation"), QStringLiteral("config/regulations/large_natural_village_renovation.json")},
        {QStringLiteral("building_construction"), QStringLiteral("config/regulations/building_construction.json")},
        {QStringLiteral("municipal_pipe_network"), QStringLiteral("config/regulations/municipal_pipe_network.json")},
        {QStringLiteral("municipal_green"), QStringLiteral("config/regulations/municipal_green.json")},
        {QStringLiteral("water_conservancy"), QStringLiteral("config/regulations/water_conservancy.json")},
        {QStringLiteral("high_standard_farmland"), QStringLiteral("config/regulations/high_standard_farmland.json")},
        {QStringLiteral("integrated_facilities"), QStringLiteral("config/regulations/integrated_facilities.json")},
        {QStringLiteral("highway"), QStringLiteral("config/regulations/highway.json")}
    };
    projectMap[QStringLiteral("note")] = QStringLiteral("P08/P09 为未来 ProjectProfile 路由；当前 ZIP 版本没有对应 legacy project_type。building_construction 的现有 ZIP 路径仍由其自身决定。\n");
    const QString mapPath = root.filePath(QStringLiteral("config/mappings/project_type_map.json"));
    if (!writeJson(mapPath, projectMap, &error)) result.errors.append(error);
    else result.generatedFiles.append(mapPath);

    const QString masterCopy = root.filePath(QStringLiteral("data/regulation_master.json"));
    if (!copyFile(options.masterPath, masterCopy, &error)) result.errors.append(error);
    else result.generatedFiles.append(masterCopy);

    QJsonObject manifest;
    manifest[QStringLiteral("schema_version")] = QStringLiteral("regulation-package.v1");
    manifest[QStringLiteral("package_type")] = QStringLiteral("inspection-and-review-system-regulations");
    manifest[QStringLiteral("generated_at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("source_master_path")] = options.masterPath;
    manifest[QStringLiteral("source_taxonomy_path")] = options.taxonomyPath;
    manifest[QStringLiteral("include_pending")] = options.includePending;
    manifest[QStringLiteral("source_rule_count")] = result.exportedRules;
    manifest[QStringLiteral("runtime_entry_count")] = result.runtimeEntries;
    manifest[QStringLiteral("pending_source_rule_count")] = result.skippedRules;
    manifest[QStringLiteral("generated_files")] = QJsonArray::fromStringList(result.generatedFiles);
    manifest[QStringLiteral("warnings")] = QJsonArray::fromStringList(result.warnings);
    manifest[QStringLiteral("errors")] = QJsonArray::fromStringList(result.errors);
    manifest[QStringLiteral("runtime_contract")] = QJsonObject{
        {QStringLiteral("root_key"), QStringLiteral("regulations")},
        {QStringLiteral("required_fields"), QJsonArray{
            QStringLiteral("code"), QStringLiteral("title"), QStringLiteral("issued_by"), QStringLiteral("level"),
            QStringLiteral("is_mandatory"), QStringLiteral("applies_to_items"), QStringLiteral("applies_to_factors"),
            QStringLiteral("keywords"), QStringLiteral("scope_clause"), QStringLiteral("requirement_text"),
            QStringLiteral("non_comply_action")
        }},
        {QStringLiteral("valid_non_comply_action"), QJsonArray{
            QStringLiteral("cap_to_general"), QStringLiteral("cap_to_negative"), QStringLiteral("warning_only")
        }}
    };
    if (!writeJson(result.manifestPath, manifest, &error)) result.errors.append(error);
    else result.generatedFiles.append(result.manifestPath);

    const QString readmePath = root.filePath(QStringLiteral("README.md"));
    const QString readme = QStringLiteral(
        "# 法规运行配置包\n\n"
        "本目录由 Qt 法规标准批量处理器自动生成，供后续 inspection-and-review-system 项目导入。\n\n"
        "## 导入方式\n\n"
        "将 `config/regulations/<project_type>.json` 复制到目标项目对应的 `config/regulations/`，"
        "或在目标项目中把该目录作为法规包来源。每个法规文件的根节点为 `regulations`。\n\n"
        "## 兼容字段\n\n"
        "每条法规包含 `code`、`title`、`issued_by`、`level`、`is_mandatory`、"
        "`applies_to_items`、`applies_to_factors`、`keywords`、`scope_clause`、"
        "`requirement_text` 和 `non_comply_action`。扩展字段保存九大类、59 个子类、"
        "专业标签、施工场景、来源条款和复核状态。\n\n"
        "## 复核状态\n\n"
        "自动抽取的条款会带有 `review_status=pending_review` 和 `pending_review=true`，"
        "这样可以先被目标程序读取，同时保留人工复核提示。确认后的条款状态为 `confirmed`。\n\n"
        "## 目录\n\n"
        "- `config/classification/project_categories.json`：P01-P09 及 59 个子类；\n"
        "- `config/classification/project_profiles.json`：后续 ProjectProfile 路由配置；\n"
        "- `config/regulations/`：common 和按项目类型拆分的运行法规包；\n"
        "- `config/regulation_schema/`：主库与运行包字段契约；\n"
        "- `data/regulation_master.json`：Qt 主规则库快照；\n"
        "- `package_manifest.json`：生成版本、计数、警告和文件清单。\n\n"
        "当前步骤只生成配置包，不修改后续 ZIP 项目源码。\n");
    if (!writeText(readmePath, readme, &error)) result.errors.append(error);
    else result.generatedFiles.append(readmePath);

    // The README is created after the first manifest write so the manifest can
    // include the final, complete generated file list and any late errors.
    manifest[QStringLiteral("generated_files")] = QJsonArray::fromStringList(result.generatedFiles);
    manifest[QStringLiteral("warnings")] = QJsonArray::fromStringList(result.warnings);
    manifest[QStringLiteral("errors")] = QJsonArray::fromStringList(result.errors);
    if (!writeJson(result.manifestPath, manifest, &error)) result.errors.append(error);

    result.success = result.errors.isEmpty();
    return result;
}
