#include "review_dialog.h"
#include "ui_reviewdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

namespace {
QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const auto entry : value.toArray()) result.append(entry.toString());
    return result;
}

QString classificationLabel(const QString& status)
{
    if (status == QStringLiteral("classified")) return QStringLiteral("单一候选");
    if (status == QStringLiteral("multiple_candidates")) return QStringLiteral("多候选");
    if (status == QStringLiteral("needs_review")) return QStringLiteral("待判定");
    return status.isEmpty() ? QStringLiteral("未提供") : status;
}

QString evidenceText(const QJsonObject& rule, const QJsonObject& names)
{
    QStringList lines;
    lines << QStringLiteral("机器分类：%1（%2，置信度：%3）")
        .arg(strings(rule.value(QStringLiteral("applicable_categories"))).join(QStringLiteral("、")),
             classificationLabel(rule.value(QStringLiteral("classification_status")).toString()),
             rule.value(QStringLiteral("category_confidence")).toString());
    lines << QStringLiteral("规则维度：") + strings(rule.value(QStringLiteral("dimensions"))).join(QStringLiteral("、"));
    lines << QStringLiteral("二级类型：") + strings(rule.value(QStringLiteral("subcategory_codes"))).join(QStringLiteral("、"));
    lines << QStringLiteral("专业标签：") + strings(rule.value(QStringLiteral("professional_tag_codes"))).join(QStringLiteral("、"));
    lines << QStringLiteral("施工场景：") + strings(rule.value(QStringLiteral("scene_codes"))).join(QStringLiteral("、"));
    lines << QStringLiteral("义务强度：") + rule.value(QStringLiteral("obligation_level")).toString();
    lines << QStringLiteral("\n复核提示：");
    const auto flags = strings(rule.value(QStringLiteral("review_flags")));
    lines << (flags.isEmpty() ? QStringLiteral("无") : flags.join(QLatin1Char('\n')));
    const auto evidence = rule.value(QStringLiteral("category_evidence")).toObject();
    lines << QStringLiteral("\n各类别命中依据：");
    for (int i = 1; i <= 9; ++i) {
        const QString id = QStringLiteral("P0%1").arg(i);
        const auto entry = evidence.value(id).toObject();
        lines << QStringLiteral("\n%1 %2").arg(id, names.value(id).toString());
        if (entry.isEmpty()) {
            lines << QStringLiteral("此文件未提供该类分类依据。");
            continue;
        }
        lines << QStringLiteral("分数：%1；核心词分数：%2；候选：%3")
            .arg(entry.value(QStringLiteral("score")).toInt())
            .arg(entry.value(QStringLiteral("core_score")).toInt())
            .arg(entry.value(QStringLiteral("eligible")).toBool() ? QStringLiteral("是") : QStringLiteral("否"));
        const auto matched = [&entry](const QString& key) {
            const QString value = strings(entry.value(key)).join(QStringLiteral("、"));
            return value.isEmpty() ? QStringLiteral("无") : value;
        };
        lines << QStringLiteral("核心命中：") + matched(QStringLiteral("core_keywords"));
        lines << QStringLiteral("辅助命中：") + matched(QStringLiteral("matched_keywords"));
        lines << QStringLiteral("排除词命中：") + matched(QStringLiteral("negative_keywords"));
    }
    return lines.join(QLatin1Char('\n'));
}
}

ReviewDialog::ReviewDialog(const QString& path, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ReviewDialog)
{
    ui->setupUi(this);
    ui->rootLayout->setStretch(1, 1);
    ui->topLayout->setStretch(1, 1);
    ui->detailLayout->setStretch(1, 1);
    ui->reviewButtonLayout->setStretch(2, 1);
    ui->bottomLayout->setStretch(2, 1);
    ui->mainSplitter->setSizes({360, 830});
    ui->mainSplitter->setStretchFactor(0, 1);
    ui->mainSplitter->setStretchFactor(1, 3);

    QString error;
    if (!m_document.load(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("无法打开复核结果"), error);
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    const auto names = m_document.categoryNames();
    for (int i = 1; i <= 9; ++i) {
        const QString id = QStringLiteral("P0%1").arg(i);
        auto* item = new QListWidgetItem(id + QStringLiteral("  ") + names.value(id).toString(), ui->reviewCategories);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    for (int i = 0; i < m_document.count(); ++i) {
        auto* item = new QListWidgetItem(ui->reviewRules);
        item->setData(Qt::UserRole, i);
        updateRow(i);
    }
    connect(ui->reviewRules, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        showRule(item ? item->data(Qt::UserRole).toInt() : -1);
    });
    connect(ui->reviewSearch, &QLineEdit::textChanged, this, [this] { filterRules(); });
    connect(ui->reviewCategories, &QListWidget::itemChanged, this, [this] { if (!m_loading) editCurrent(false); });
    connect(ui->reviewNote, &QPlainTextEdit::textChanged, this, [this] { if (!m_loading) editCurrent(false); });
    connect(ui->reviewConfirm, &QPushButton::clicked, this, [this] { editCurrent(true); });
    connect(ui->reviewPending, &QPushButton::clicked, this, [this] { editCurrent(false); });
    connect(ui->saveButton, &QPushButton::clicked, this, [this] { saveResult(); });
    connect(ui->saveAsButton, &QPushButton::clicked, this, [this] { saveResult(true); });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ReviewDialog::reject);
    if (m_document.count() > 0) ui->reviewRules->setCurrentRow(0);
    else showRule(-1);
    updateSummary();
}

ReviewDialog::~ReviewDialog()
{
    delete ui;
}

void ReviewDialog::updateRow(int index)
{
    const auto rule = m_document.rule(index);
    const auto review = rule.value(QStringLiteral("human_review")).toObject();
    const bool confirmed = review.value(QStringLiteral("status")).toString() == QStringLiteral("confirmed");
    QString preview = rule.value(QStringLiteral("rule_title")).toString();
    if (preview.isEmpty()) preview = rule.value(QStringLiteral("text")).toString();
    preview = preview.simplified().left(70);
    auto* item = ui->reviewRules->item(index);
    item->setText(QStringLiteral("%1  [%2] %3\n%4")
        .arg(index + 1).arg(confirmed ? QStringLiteral("已确认") : QStringLiteral("待复核"),
            rule.value(QStringLiteral("article")).toString(), preview));
    item->setToolTip(rule.value(QStringLiteral("rule_id")).toString());
}

void ReviewDialog::filterRules()
{
    const QString search = ui->reviewSearch->text().trimmed();
    int firstVisible = -1;
    for (int i = 0; i < m_document.count(); ++i) {
        const auto rule = m_document.rule(i);
        const QString text = rule.value(QStringLiteral("text")).toString() + QLatin1Char('\n')
            + rule.value(QStringLiteral("article")).toString() + QLatin1Char('\n')
            + rule.value(QStringLiteral("rule_id")).toString();
        const bool visible = search.isEmpty() || text.contains(search, Qt::CaseInsensitive);
        ui->reviewRules->item(i)->setHidden(!visible);
        if (visible && firstVisible < 0) firstVisible = i;
    }
    if (m_currentIndex < 0 || ui->reviewRules->item(m_currentIndex)->isHidden()) {
        ui->reviewRules->setCurrentRow(firstVisible);
        if (firstVisible < 0) showRule(-1);
    }
}

void ReviewDialog::showRule(int index)
{
    m_loading = true;
    m_currentIndex = index;
    ui->detailPanel->setEnabled(index >= 0);
    if (index < 0) {
        ui->sourceInfo->setText(QStringLiteral("没有匹配的条款"));
        ui->reviewOriginalText->clear();
        ui->reviewEvidence->clear();
        ui->reviewNote->clear();
        ui->reviewStatus->clear();
        for (int i = 0; i < ui->reviewCategories->count(); ++i) ui->reviewCategories->item(i)->setCheckState(Qt::Unchecked);
        m_loading = false;
        return;
    }
    const auto rule = m_document.rule(index);
    const auto review = rule.value(QStringLiteral("human_review")).toObject();
    const int start = rule.value(QStringLiteral("source_page")).toInt();
    const int end = qMax(start, rule.value(QStringLiteral("source_page_end")).toInt());
    const QString pages = start == 0 ? QStringLiteral("未提供页码")
        : start == end ? QStringLiteral("第 %1 页").arg(start) : QStringLiteral("第 %1—%2 页").arg(start).arg(end);
    ui->sourceInfo->setText(QStringLiteral("%1 | %2\n来源：%3 | %4")
        .arg(rule.value(QStringLiteral("rule_id")).toString(), rule.value(QStringLiteral("article")).toString(),
             rule.value(QStringLiteral("source_file")).toString(), pages));
    ui->reviewOriginalText->setPlainText(rule.value(QStringLiteral("text")).toString());
    ui->reviewEvidence->setPlainText(evidenceText(rule, m_document.categoryNames()));
    const auto selected = strings((review.isEmpty() ? rule : review).value(QStringLiteral("applicable_categories")));
    for (int i = 0; i < ui->reviewCategories->count(); ++i) {
        auto* item = ui->reviewCategories->item(i);
        item->setCheckState(selected.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    ui->reviewNote->setPlainText(review.value(QStringLiteral("note")).toString());
    ui->reviewStatus->setText(review.value(QStringLiteral("status")).toString() == QStringLiteral("confirmed")
        ? QStringLiteral("已确认") : review.isEmpty() ? QStringLiteral("待复核（预选机器候选）") : QStringLiteral("待复核（人工草稿）"));
    m_loading = false;
}

bool ReviewDialog::editCurrent(bool confirmed)
{
    if (m_loading || m_currentIndex < 0) return false;
    QStringList selected;
    for (int i = 0; i < ui->reviewCategories->count(); ++i) {
        const auto* item = ui->reviewCategories->item(i);
        if (item->checkState() == Qt::Checked) selected.append(item->data(Qt::UserRole).toString());
    }
    QString error;
    if (!m_document.setReview(m_currentIndex, selected, ui->reviewNote->toPlainText(), confirmed, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法确认"), error);
        return false;
    }
    updateRow(m_currentIndex);
    updateSummary();
    ui->reviewStatus->setText(confirmed ? QStringLiteral("已确认") : QStringLiteral("待复核（人工草稿）"));
    return true;
}

void ReviewDialog::updateSummary()
{
    const int confirmed = m_document.confirmedCount();
    ui->progressLabel->setText(QStringLiteral("共 %1 条 · 已确认 %2 条 · 待复核 %3 条")
        .arg(m_document.count()).arg(confirmed).arg(m_document.count() - confirmed));
    setWindowTitle(QStringLiteral("人工复核 — %1%2").arg(QFileInfo(m_document.sourcePath()).fileName(),
        m_document.isDirty() ? QStringLiteral(" * 未保存") : QString()));
    if (m_document.isDirty()) ui->saveInfo->setText(QStringLiteral("当前修改尚未保存。"));
}

bool ReviewDialog::saveResult(bool saveAs)
{
    QString path = m_document.savedPath();
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存人工复核结果（D:\\Code\\Codex）"),
            m_document.suggestedSavePath(), QStringLiteral("JSON 复核结果 (*.json)"));
        if (path.isEmpty()) return false;
    }
    QString error;
    if (!m_document.save(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return false;
    }
    updateSummary();
    ui->saveInfo->setText(QStringLiteral("已保存：") + path);
    return true;
}

void ReviewDialog::reject()
{
    if (m_document.isDirty()) {
        const auto choice = QMessageBox::question(this, QStringLiteral("有未保存修改"),
            QStringLiteral("是否保存人工复核结果？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save && !saveResult()) return;
    }
    QDialog::reject();
}
