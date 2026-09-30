#include "rule_library_dialog.h"
#include "rule_engine.h"
#include "rule_library.h"
#include "ui_rulelibrarydialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QSignalBlocker>
#include <QTextStream>

#include <algorithm>

namespace {
QStringList jsonStrings(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text)) result.append(text);
    }
    return result;
}

QString firstNonEmpty(const QJsonObject& object, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = object.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}

QString statusLabel(const QString& status)
{
    if (status == QStringLiteral("pending_review")) return QStringLiteral("待复核");
    if (status == QStringLiteral("confirmed")) return QStringLiteral("已确认");
    if (status == QStringLiteral("rejected")) return QStringLiteral("已拒绝");
    if (status == QStringLiteral("deprecated")) return QStringLiteral("已废止");
    return status.isEmpty() ? QStringLiteral("未标记") : status;
}

QString sourceLine(const QJsonObject& source)
{
    QString line = source.value(QStringLiteral("file")).toString();
    const int page = source.value(QStringLiteral("page")).toInt();
    const int pageEnd = source.value(QStringLiteral("page_end")).toInt();
    if (page > 0) {
        line += QStringLiteral("  第") + QString::number(page);
        if (pageEnd > page) line += QStringLiteral("-") + QString::number(pageEnd);
        line += QStringLiteral("页");
    }
    const QString article = source.value(QStringLiteral("article")).toString();
    if (!article.isEmpty()) line += QStringLiteral("  ") + article;
    return line;
}
}

RuleLibraryDialog::RuleLibraryDialog(QWidget* parent, const QString& libraryPath, const QString& indexDirectory)
    : QDialog(parent)
    , ui(new Ui::RuleLibraryDialog)
    , m_libraryPath(libraryPath)
    , m_indexDirectory(indexDirectory)
{
    ui->setupUi(this);
    ui->rootLayout->setStretch(1, 1);
    ui->filterLayout->setStretch(1, 1);
    ui->mainSplitter->setSizes({360, 900});
    ui->mainSplitter->setStretchFactor(0, 1);
    ui->mainSplitter->setStretchFactor(1, 3);

    connect(ui->refreshButton, &QPushButton::clicked, this, [this] { reloadLibrary(true); });
    connect(ui->librarySearch, &QLineEdit::textChanged, this, [this] { refreshList(); });
    connect(ui->categoryFilter, &QComboBox::currentTextChanged, this, [this] { refreshList(); });
    connect(ui->dimensionFilter, &QComboBox::currentTextChanged, this, [this] { refreshList(); });
    connect(ui->statusFilter, &QComboBox::currentTextChanged, this, [this] { refreshList(); });
    connect(ui->libraryRuleList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) { showRule(current); });
    connect(ui->libraryButtonBox, &QDialogButtonBox::rejected, this, &RuleLibraryDialog::reject);

    reloadLibrary(false);
}

RuleLibraryDialog::~RuleLibraryDialog()
{
    delete ui;
}

QString RuleLibraryDialog::findProjectRoot() const
{
    QDir directory(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 8; ++level) {
        if (QFileInfo::exists(directory.filePath(QStringLiteral("config/rules.json")))) {
            return directory.absolutePath();
        }
        if (!directory.cdUp()) break;
    }
    const QString preferred = QStringLiteral("D:/Code/Codex/标书评分项目");
    if (QFileInfo::exists(QDir(preferred).filePath(QStringLiteral("config/rules.json")))) {
        return QDir::toNativeSeparators(preferred);
    }
    return QDir::currentPath();
}

bool RuleLibraryDialog::loadLibrary(QString* error)
{
    const QString projectRoot = findProjectRoot();
    const QString configPath = QDir(projectRoot).filePath(QStringLiteral("config/rules.json"));
    if (m_libraryPath.trimmed().isEmpty()) {
        m_libraryPath = QDir(projectRoot).filePath(QStringLiteral("data/rule_library.json"));
    }
    if (m_indexDirectory.trimmed().isEmpty()) {
        m_indexDirectory = QDir(projectRoot).filePath(QStringLiteral("data/indexes"));
    }

    RuleEngine engine;
    QString engineError;
    if (!engine.load(configPath, &engineError)) {
        if (error) *error = engineError;
        return false;
    }
    m_categoryNames = QJsonObject();
    for (const QString& code : engine.categoryCodes()) {
        m_categoryNames[code] = engine.categoryName(code);
    }

    RuleLibraryStore store;
    QString storeError;
    if (!store.open(m_libraryPath, m_indexDirectory, m_categoryNames, &storeError)) {
        if (error) *error = storeError;
        return false;
    }
    m_rules = store.masterJson().value(QStringLiteral("rules")).toArray();
    return true;
}

void RuleLibraryDialog::reloadLibrary(bool showError)
{
    QString error;
    if (!loadLibrary(&error)) {
        m_rules = QJsonArray();
        m_categoryNames = QJsonObject();
        populateFilters();
        refreshList();
        ui->librarySummaryLabel->setText(QStringLiteral("规则库加载失败：") + error);
        ui->sourceInfo->setText(QStringLiteral("无法读取主规则库，请检查配置和 JSON 文件。"));
        if (showError) QMessageBox::warning(this, QStringLiteral("规则库加载失败"), error);
        return;
    }
    populateFilters();
    refreshList();
}

void RuleLibraryDialog::populateFilters()
{
    const QString oldCategory = ui->categoryFilter->currentData().toString();
    const QString oldDimension = ui->dimensionFilter->currentData().toString();
    const QString oldStatus = ui->statusFilter->currentData().toString();

    {
        const QSignalBlocker blocker(ui->categoryFilter);
        ui->categoryFilter->clear();
        ui->categoryFilter->addItem(QStringLiteral("全部"), QString());
        QStringList codes = m_categoryNames.keys();
        if (codes.isEmpty()) {
            for (int index = 1; index <= 9; ++index) codes.append(QStringLiteral("P0%1").arg(index));
        }
        std::sort(codes.begin(), codes.end());
        for (const QString& code : codes) {
            const QString name = m_categoryNames.value(code).toString();
            ui->categoryFilter->addItem(name.isEmpty() ? code : code + QStringLiteral(" ") + name, code);
        }
        const int index = ui->categoryFilter->findData(oldCategory);
        ui->categoryFilter->setCurrentIndex(index >= 0 ? index : 0);
    }

    QSet<QString> dimensions;
    for (const QJsonValue& value : m_rules) {
        for (const QString& dimension : jsonStrings(value.toObject().value(QStringLiteral("dimensions")))) {
            dimensions.insert(dimension);
        }
    }
    const QStringList defaultDimensions = {
        QStringLiteral("施工组织"), QStringLiteral("质量"), QStringLiteral("安全"),
        QStringLiteral("进度"), QStringLiteral("资源"), QStringLiteral("环境"), QStringLiteral("合同招投标")
    };
    for (const QString& dimension : defaultDimensions) dimensions.insert(dimension);
    {
        const QSignalBlocker blocker(ui->dimensionFilter);
        ui->dimensionFilter->clear();
        ui->dimensionFilter->addItem(QStringLiteral("全部"), QString());
        QStringList values = dimensions.values();
        std::sort(values.begin(), values.end());
        for (const QString& value : values) ui->dimensionFilter->addItem(value, value);
        const int index = ui->dimensionFilter->findData(oldDimension);
        ui->dimensionFilter->setCurrentIndex(index >= 0 ? index : 0);
    }

    {
        const QSignalBlocker blocker(ui->statusFilter);
        ui->statusFilter->clear();
        ui->statusFilter->addItem(QStringLiteral("全部"), QString());
        ui->statusFilter->addItem(QStringLiteral("待复核"), QStringLiteral("pending_review"));
        ui->statusFilter->addItem(QStringLiteral("已确认"), QStringLiteral("confirmed"));
        ui->statusFilter->addItem(QStringLiteral("疑似重复"), QStringLiteral("similar"));
        const int index = ui->statusFilter->findData(oldStatus);
        ui->statusFilter->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void RuleLibraryDialog::refreshList()
{
    const QString search = ui->librarySearch->text().trimmed();
    const QString category = ui->categoryFilter->currentData().toString();
    const QString dimension = ui->dimensionFilter->currentData().toString();
    const QString status = ui->statusFilter->currentData().toString();
    int matched = 0;
    {
        const QSignalBlocker blocker(ui->libraryRuleList);
        ui->libraryRuleList->clear();

        for (const QJsonValue& value : m_rules) {
            const QJsonObject rule = value.toObject();
            const QString id = firstNonEmpty(rule, {QStringLiteral("rule_id"), QStringLiteral("source_rule_id")});
            const QString title = firstNonEmpty(rule, {QStringLiteral("title"), QStringLiteral("rule_title")});
            const QString text = rule.value(QStringLiteral("text")).toString();
            QStringList categories = jsonStrings(rule.value(QStringLiteral("categories")));
            if (categories.isEmpty()) categories = jsonStrings(rule.value(QStringLiteral("applicable_categories")));
            const QStringList dimensions = jsonStrings(rule.value(QStringLiteral("dimensions")));
            const QString rawStatus = rule.value(QStringLiteral("status")).toString();
            const bool similar = !rule.value(QStringLiteral("similar_rule_ids")).toArray().isEmpty();
            const QStringList flags = jsonStrings(rule.value(QStringLiteral("review_flags")));

            if (!category.isEmpty() && !categories.contains(category)) continue;
            if (!dimension.isEmpty() && !dimensions.contains(dimension)) continue;
            if (status == QStringLiteral("similar") && !similar && !flags.join(QStringLiteral(" ")).contains(QStringLiteral("重复"))) continue;
            if (!status.isEmpty() && status != QStringLiteral("similar") && rawStatus != status) continue;
            if (!search.isEmpty()) {
                const QString haystack = id + QStringLiteral(" ") + title + QStringLiteral(" ") + text + QStringLiteral(" ")
                        + categories.join(QStringLiteral(" ")) + QStringLiteral(" ") + dimensions.join(QStringLiteral(" "));
                if (!haystack.contains(search, Qt::CaseInsensitive)) continue;
            }

            QString display = id;
            if (!title.isEmpty()) display += QStringLiteral("  ") + title;
            auto* item = new QListWidgetItem(display, ui->libraryRuleList);
            item->setData(Qt::UserRole, id);
            item->setToolTip(text);
            ++matched;
        }
    }

    ui->librarySummaryLabel->setText(QStringLiteral("规则总数：%1；当前显示：%2\n主库：%3")
                                     .arg(m_rules.size()).arg(matched).arg(m_libraryPath));
    if (ui->libraryRuleList->count() > 0) {
        ui->libraryRuleList->setCurrentRow(0);
        showRule(ui->libraryRuleList->currentItem());
    } else {
        clearDetails();
    }
}

void RuleLibraryDialog::clearDetails()
{
    ui->sourceInfo->setText(QStringLiteral("请选择规则查看来源和机器分类依据。"));
    ui->ruleTextEdit->clear();
    ui->ruleCategoriesList->clear();
    ui->ruleEvidenceEdit->clear();
    ui->ruleIdEdit->clear();
    ui->sourceFileEdit->clear();
    ui->sourcePageEdit->clear();
    ui->statusEdit->clear();
    ui->ruleNoteEdit->clear();
}

void RuleLibraryDialog::showRule(const QListWidgetItem* item)
{
    if (!item) {
        clearDetails();
        return;
    }
    const QString ruleId = item->data(Qt::UserRole).toString();
    QJsonObject rule;
    for (const QJsonValue& value : m_rules) {
        const QJsonObject candidate = value.toObject();
        if (firstNonEmpty(candidate, {QStringLiteral("rule_id"), QStringLiteral("source_rule_id")}) == ruleId) {
            rule = candidate;
            break;
        }
    }
    if (rule.isEmpty()) {
        clearDetails();
        return;
    }

    const QJsonArray sources = rule.value(QStringLiteral("sources")).toArray();
    QStringList sourceLines;
    for (const QJsonValue& value : sources) {
        const QString line = sourceLine(value.toObject());
        if (!line.isEmpty()) sourceLines.append(line);
    }
    if (sourceLines.isEmpty()) {
        QJsonObject fallback;
        fallback[QStringLiteral("file")] = rule.value(QStringLiteral("source_file"));
        fallback[QStringLiteral("page")] = rule.value(QStringLiteral("source_page"));
        fallback[QStringLiteral("page_end")] = rule.value(QStringLiteral("source_page_end"));
        fallback[QStringLiteral("article")] = rule.value(QStringLiteral("article"));
        const QString line = sourceLine(fallback);
        if (!line.isEmpty()) sourceLines.append(line);
    }
    ui->sourceInfo->setText(QStringLiteral("来源数量：%1\n%2").arg(sourceLines.size()).arg(sourceLines.join(QStringLiteral("\n"))));
    ui->ruleTextEdit->setPlainText(rule.value(QStringLiteral("text")).toString());

    ui->ruleCategoriesList->clear();
    QStringList categories = jsonStrings(rule.value(QStringLiteral("categories")));
    if (categories.isEmpty()) categories = jsonStrings(rule.value(QStringLiteral("applicable_categories")));
    const QStringList categoryNames = jsonStrings(rule.value(QStringLiteral("category_names")));
    for (int index = 0; index < categories.size(); ++index) {
        const QString code = categories.at(index);
        QString name = index < categoryNames.size() ? categoryNames.at(index) : m_categoryNames.value(code).toString();
        ui->ruleCategoriesList->addItem(name.isEmpty() ? code : code + QStringLiteral(" ") + name);
    }

    QString evidence;
    QTextStream evidenceStream(&evidence);
    evidenceStream << "状态：" << statusLabel(rule.value(QStringLiteral("status")).toString()) << "\n";
    evidenceStream << "文档编号：" << rule.value(QStringLiteral("document_code")).toString() << "\n";
    evidenceStream << "文档级别：" << rule.value(QStringLiteral("document_level")).toString() << "\n";
    evidenceStream << "维度：" << jsonStrings(rule.value(QStringLiteral("dimensions"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "二级类型：" << jsonStrings(rule.value(QStringLiteral("subcategory_codes"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "专业标签：" << jsonStrings(rule.value(QStringLiteral("professional_tag_codes"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "施工场景：" << jsonStrings(rule.value(QStringLiteral("scene_codes"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "义务强度：" << rule.value(QStringLiteral("obligation_level")).toString() << "\n";
    evidenceStream << "条件：" << jsonStrings(rule.value(QStringLiteral("conditions"))).join(QStringLiteral("；")) << "\n";
    evidenceStream << "证据词：" << jsonStrings(rule.value(QStringLiteral("evidence"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "命中关键词：" << jsonStrings(rule.value(QStringLiteral("matched_keywords"))).join(QStringLiteral("、")) << "\n";
    evidenceStream << "复核提示：" << jsonStrings(rule.value(QStringLiteral("review_flags"))).join(QStringLiteral("；")) << "\n\n";
    evidenceStream << QJsonDocument(rule.value(QStringLiteral("category_evidence")).toObject()).toJson(QJsonDocument::Indented);
    ui->ruleEvidenceEdit->setPlainText(evidence);

    ui->ruleIdEdit->setText(firstNonEmpty(rule, {QStringLiteral("rule_id"), QStringLiteral("source_rule_id")}));
    ui->sourceFileEdit->setText(sourceLines.isEmpty() ? rule.value(QStringLiteral("source_file")).toString()
                                                      : sourceLines.first().section(QStringLiteral("  "), 0, 0));
    const int page = !sources.isEmpty() ? sources.first().toObject().value(QStringLiteral("page")).toInt()
                                       : rule.value(QStringLiteral("source_page")).toInt();
    const int pageEnd = !sources.isEmpty() ? sources.first().toObject().value(QStringLiteral("page_end")).toInt()
                                           : rule.value(QStringLiteral("source_page_end")).toInt();
    ui->sourcePageEdit->setText(page <= 0 ? QString() : (pageEnd > page ? QStringLiteral("%1-%2").arg(page).arg(pageEnd) : QString::number(page)));
    ui->statusEdit->setText(statusLabel(rule.value(QStringLiteral("status")).toString()));
    ui->ruleNoteEdit->setPlainText(rule.value(QStringLiteral("notes")).toString());
}
