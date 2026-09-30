#include "document_processor.h"
#include "regulation_package_exporter.h"
#include "rule_library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextStream>
#include <QXmlStreamReader>
#include <limits>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QString csvCell(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString mdCell(QString value)
{
    value.replace('|', QStringLiteral("\\|"));
    value.replace('\n', QChar(' '));
    return value;
}

bool writeUtf8(const QString& path, const QByteArray& data, QString* error = nullptr)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入：") + path;
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("写入失败：") + path;
        return false;
    }
    return true;
}

QString substantivePdfText(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("(?m)^\\s*(?:第\\s*)?[0-9０-９一二三四五六七八九十百千万零〇]+\\s*(?:页|/\\s*[0-9０-９]+)?\\s*$")));
    text.remove(QRegularExpression(QStringLiteral("水印|仅供参考|内部资料|版权所有|扫描全能王|试用版|草稿|资料来源于互联网|由网友分享提供|请在下载后\\s*24\\s*小时内删除|如果您觉得满意|购买正版")));
    text.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}\\p{N}]+")));
    return text;
}

struct PdfTextQuality {
    bool suspiciousEncoding = false;
    double fullwidthRatio = 0.0;
    double hanRatio = 0.0;
    double spacedLatinRatio = 0.0;
};

int countFullwidthAscii(const QString& text)
{
    int count = 0;
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if (code >= 0xFF01 && code <= 0xFF5E) ++count;
    }
    return count;
}

PdfTextQuality inspectPdfTextQuality(const QString& text)
{
    PdfTextQuality quality;
    const QString sample = text.normalized(QString::NormalizationForm_KC);
    const int fullwidthCount = countFullwidthAscii(text);
    int latinCount = 0;
    auto iterator = QRegularExpression(QStringLiteral("[A-Za-z]")).globalMatch(sample);
    while (iterator.hasNext()) { iterator.next(); ++latinCount; }
    int hanCount = 0;
    iterator = QRegularExpression(QStringLiteral("[\\x{3400}-\\x{9FFF}]")).globalMatch(sample);
    while (iterator.hasNext()) { iterator.next(); ++hanCount; }
    int spacedLatinCount = 0;
    iterator = QRegularExpression(QStringLiteral("[A-Za-z][ \\t]+[A-Za-z]")).globalMatch(sample);
    while (iterator.hasNext()) { iterator.next(); ++spacedLatinCount; }
    const int denominator = qMax(1, fullwidthCount + latinCount + hanCount);
    quality.fullwidthRatio = double(fullwidthCount) / denominator;
    quality.hanRatio = double(hanCount) / denominator;
    quality.spacedLatinRatio = latinCount > 0 ? double(spacedLatinCount) / latinCount : 0.0;
    const double fullwidthRawRatio = double(fullwidthCount) / qMax(1, text.size());
    const double hanRawRatio = double(hanCount) / qMax(1, text.size());
    quality.suspiciousEncoding = (fullwidthRawRatio >= 0.06 && quality.spacedLatinRatio >= 0.04
                                  && hanRawRatio < 0.50)
        || (fullwidthRawRatio >= 0.10 && hanRawRatio < 0.25);
    return quality;
}

bool isUnreadablePdfFragment(const QString& text)
{
    const QString sample = text.normalized(QString::NormalizationForm_KC);
    const int fullwidth = countFullwidthAscii(text);
    int latin = 0, han = 0, spacedLatin = 0;
    auto countMatches = [&sample](const QString& pattern) {
        int count = 0;
        auto it = QRegularExpression(pattern).globalMatch(sample);
        while (it.hasNext()) { it.next(); ++count; }
        return count;
    };
    latin = countMatches(QStringLiteral("[A-Za-z]"));
    han = countMatches(QStringLiteral("[\\x{3400}-\\x{9FFF}]"));
    spacedLatin = countMatches(QStringLiteral("[A-Za-z][ \\t]+[A-Za-z]"));
    const int denominator = qMax(1, fullwidth + latin + han);
    const double fullwidthRatio = double(fullwidth) / denominator;
    const double hanRatio = double(han) / denominator;
    const double spacedLatinRatio = latin > 0 ? double(spacedLatin) / latin : 0.0;
    return hanRatio < 0.25 && (fullwidthRatio >= 0.04 || spacedLatinRatio >= 0.18);
}

bool isNoiseClause(const QString& text)
{
    QString value = text.normalized(QString::NormalizationForm_KC).simplified();
    if (value.isEmpty()) return true;
    const QString compact = QString(value).remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
    if (compact.isEmpty()) return true;
    const QStringList watermarkPhrases = {
        QStringLiteral("资料来源于互联网"), QStringLiteral("由网友分享提供"),
        QStringLiteral("请在下载后24小时内删除"), QStringLiteral("如果您觉得满意"),
        QStringLiteral("购买正版")
    };
    int watermarkPhraseCount = 0;
    for (QString phrase : watermarkPhrases) {
        phrase.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
        if (compact.contains(phrase)) ++watermarkPhraseCount;
    }
    // These phrases occur together in common download-site overlays. Requiring
    // multiple markers avoids dropping legitimate clauses that mention one phrase.
    if (watermarkPhraseCount >= 2) return true;
    if (QRegularExpression(QStringLiteral("^(?:第)?[0-9]+(?:页)?$"), QRegularExpression::CaseInsensitiveOption).match(compact).hasMatch()) return true;
    if (QRegularExpression(QStringLiteral("^(?:目录|目[录錄]|水印|仅供参考|内部资料|版权所有|扫描全能王|试用版)+$"), QRegularExpression::CaseInsensitiveOption).match(compact).hasMatch()) return true;
    return false;
}
}

DocumentProcessor::DocumentProcessor(QObject* parent)
    : QObject(parent)
{
}

QString DocumentProcessor::normalize(const QString& value) const
{
    QString text = value;
    text.replace(QChar(0x00A0), QChar(' '));
    text.replace(QChar(0x3000), QChar(' '));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QString DocumentProcessor::resolveTool(const QString& requested, const QStringList& names)
{
    if (!requested.trimmed().isEmpty() && QFileInfo::exists(requested)) return QDir::toNativeSeparators(requested);
    for (const QString& name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return found;
    }
    const QStringList knownPaths = {
        QStringLiteral("D:/software/7Zip/7-Zip/7z.exe"),
        QStringLiteral("D:/software/poppler/Library/bin/pdftotext.exe"),
        QStringLiteral("D:/software/poppler/bin/pdftotext.exe"),
        QStringLiteral("D:/Code/Codex/标书评分项目/tools/poppler/Library/bin/pdftotext.exe"),
        QStringLiteral("D:/Code/Codex/标书评分项目/tools/poppler/bin/pdftotext.exe")
    };
    for (const QString& path : knownPaths) {
        const QString fileName = QFileInfo(path).fileName().toLower();
        for (const QString& name : names) {
            if (fileName == name.toLower() && QFileInfo::exists(path)) return QDir::toNativeSeparators(path);
        }
    }
    return {};
}

QString DocumentProcessor::findTool(const QString& requested, const QStringList& names) const
{
    return resolveTool(requested, names);
}

bool DocumentProcessor::validateOutputDirectory(const QString& path, QString* error)
{
    const QString root = QDir::cleanPath(QStringLiteral("D:/Code/Codex"));
    const QString candidate = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    const QString foldedRoot = root.toCaseFolded();
    const QString foldedCandidate = candidate.toCaseFolded();
    const bool allowed = !candidate.isEmpty() &&
                         (foldedCandidate == foldedRoot || foldedCandidate.startsWith(foldedRoot + QChar('/')));
    if (!allowed && error) {
        *error = QStringLiteral("输出目录必须位于 D:\\Code\\Codex 下：") + path;
    }
    return allowed;
}

QString DocumentProcessor::readPlainText(const QString& path, DocumentResult* result) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result->warnings.append(QStringLiteral("无法读取 TXT：") + file.errorString());
        return {};
    }
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result->warnings.append(QStringLiteral("TXT 读取中断：") + file.errorString());
        return {};
    }
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    QString encodingName = QStringLiteral("UTF-8");
    int bomLength = 0;
    if (bytes.startsWith(QByteArray::fromHex("fffe0000"))) {
        encoding = QStringConverter::Utf32LE;
        encodingName = QStringLiteral("UTF-32LE");
        bomLength = 4;
    } else if (bytes.startsWith(QByteArray::fromHex("0000feff"))) {
        encoding = QStringConverter::Utf32BE;
        encodingName = QStringLiteral("UTF-32BE");
        bomLength = 4;
    } else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        encoding = QStringConverter::Utf16LE;
        encodingName = QStringLiteral("UTF-16LE");
        bomLength = 2;
    } else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        encoding = QStringConverter::Utf16BE;
        encodingName = QStringLiteral("UTF-16BE");
        bomLength = 2;
    } else if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) {
        bomLength = 3;
    }
    bytes.remove(0, bomLength);
    // Stateless decoding reports incomplete trailing sequences as errors.
    QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
    QString text = decoder(bytes);
    if (decoder.hasError()) {
        text.clear();
#ifdef Q_OS_WIN
        // Chinese TXT files without a BOM commonly use GBK/GB18030. Use an
        // explicit code page, independent of the Windows display language.
        if (bomLength == 0 && bytes.size() <= std::numeric_limits<int>::max()) {
            const int length = MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS,
                                                  bytes.constData(), int(bytes.size()), nullptr, 0);
            if (length > 0) {
                std::wstring wide(size_t(length), L'\0');
                if (MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.constData(),
                                        int(bytes.size()), wide.data(), length) == length) {
                    text = QString::fromStdWString(wide);
                    encodingName = QStringLiteral("GB18030/GBK（推定）");
                    result->warnings.append(QStringLiteral("无编码标记，按 GB18030/GBK 读取；请核对导出的原文。"));
                }
            }
        }
#endif
        if (text.isEmpty()) {
            result->warnings.append(QStringLiteral("TXT 编码无效或不支持，请另存为 UTF-8 后重试。"));
            return {};
        }
    }
    for (const QChar ch : text) {
        if ((ch.unicode() < 0x20 && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f')
            || ch == QChar(0x7f)) {
            result->warnings.append(QStringLiteral("TXT 含异常控制字符，可能是二进制文件或未带编码标记的 UTF-16；请另存为 UTF-8。"));
            return {};
        }
    }
    result->sourceEncoding = encodingName;
    result->extractedText = text;
    if (text.trimmed().isEmpty()) result->warnings.append(QStringLiteral("TXT 为空或仅包含空白字符。"));
    return normalize(text);
}

QString DocumentProcessor::readDocxText(const QString& path, const QString& sevenZip, DocumentResult* result) const
{
    QStringList* warnings = &result->warnings;
    const QString requested = sevenZip.trimmed();
    if (!requested.isEmpty() && !QFileInfo(requested).isFile()) {
        warnings->append(QStringLiteral("指定的 7-Zip 程序不存在：") + requested);
        return {};
    }
    const QString tool = findTool(requested, {QStringLiteral("7z.exe"), QStringLiteral("7z")});
    if (tool.isEmpty()) {
        warnings->append(QStringLiteral("未找到 7-Zip，无法读取 DOCX。请在界面中指定 7z.exe。"));
        return {};
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(tool, {QStringLiteral("x"), QStringLiteral("-so"), path, QStringLiteral("word/document.xml")});
    if (!process.waitForStarted(10000)) {
        warnings->append(QStringLiteral("无法启动 7-Zip：") + process.errorString());
        return {};
    }
    // A password prompt must not hold up a batch on an unattended process.
    process.closeWriteChannel();
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        warnings->append(QStringLiteral("DOCX 提取超时。"));
        return {};
    }
    const QByteArray xmlData = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || xmlData.isEmpty()) {
        const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        warnings->append(QStringLiteral("DOCX 提取失败，文件可能损坏、加密或缺少 word/document.xml。")
                         + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
        return {};
    }

    QXmlStreamReader xml(xmlData);
    QString text;
    QList<bool> rowHasCell;
    bool inBody = false;
    bool foundDocument = false;
    bool foundBody = false;
    const auto addWarning = [warnings](const QString& warning) {
        if (!warnings->contains(warning)) warnings->append(warning);
    };
    const auto isWordElement = [&xml] {
        return xml.namespaceUri() == QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main")
            || xml.namespaceUri() == QStringLiteral("http://purl.oclc.org/ooxml/wordprocessingml/main");
    };
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString name = xml.name().toString();
            if (xml.namespaceUri() == QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006")
                && name == QStringLiteral("AlternateContent")) {
                addWarning(QStringLiteral("含兼容性图形内容，其文字未提取，请核对原文件。"));
                xml.skipCurrentElement();
                continue;
            }
            if (!isWordElement()) continue;
            if (name == QStringLiteral("document")) {
                foundDocument = true;
                continue;
            }
            if (name == QStringLiteral("body") && foundDocument) {
                foundBody = true;
                inBody = true;
                continue;
            }
            if (!inBody) continue;
            if (name == QStringLiteral("pPr")) {
                while (xml.readNextStartElement()) {
                    if (isWordElement() && xml.name() == QStringLiteral("numPr")) {
                        addWarning(QStringLiteral("含自动列表编号；本步只提取文字，自动编号需对照原文件。"));
                    }
                    xml.skipCurrentElement();
                }
                continue;
            }
            if (name == QStringLiteral("rPr") || name == QStringLiteral("tblPr") || name == QStringLiteral("tblGrid")
                || name == QStringLiteral("trPr") || name == QStringLiteral("tcPr") || name == QStringLiteral("sectPr")) {
                xml.skipCurrentElement();
                continue;
            }
            if (name == QStringLiteral("del") || name == QStringLiteral("moveFrom")) {
                addWarning(QStringLiteral("含修订记录；已跳过删除/移出内容，请核对原文件。"));
                xml.skipCurrentElement();
                continue;
            }
            if (name == QStringLiteral("ins") || name == QStringLiteral("moveTo")) {
                addWarning(QStringLiteral("含修订记录；已读取插入/移入内容，请核对原文件。"));
            }
            if (name == QStringLiteral("drawing") || name == QStringLiteral("pict") || name == QStringLiteral("object")
                || name == QStringLiteral("altChunk")) {
                addWarning(QStringLiteral("含图片、文本框或嵌入对象；其中的文字未提取，请核对原文件。"));
                xml.skipCurrentElement();
                continue;
            }
            if (name == QStringLiteral("tbl")) {
                rowHasCell.append(false);
                if (!text.isEmpty() && !text.endsWith(QStringLiteral("\n\n"))) text.append(QStringLiteral("\n\n"));
            } else if (name == QStringLiteral("tr")) {
                if (!rowHasCell.isEmpty()) rowHasCell.last() = false;
            } else if (name == QStringLiteral("tc")) {
                if (!rowHasCell.isEmpty()) {
                    if (rowHasCell.last()) text.append(QChar('\t'));
                    rowHasCell.last() = true;
                }
            } else if (name == QStringLiteral("t")) {
                text.append(xml.readElementText());
            } else if (name == QStringLiteral("tab")) {
                text.append(QChar('\t'));
            } else if (name == QStringLiteral("br") || name == QStringLiteral("cr")) {
                text.append(QChar('\n'));
            } else if (name == QStringLiteral("noBreakHyphen")) {
                text.append(QChar(0x2011));
            } else if (name == QStringLiteral("softHyphen")) {
                text.append(QChar(0x00AD));
            }
        } else if (xml.isEndElement() && isWordElement() && inBody) {
            const QString name = xml.name().toString();
            if (name == QStringLiteral("body")) {
                inBody = false;
            } else if (name == QStringLiteral("p")) {
                text.append(rowHasCell.isEmpty() ? QStringLiteral("\n\n") : QStringLiteral("\n"));
            } else if (name == QStringLiteral("tc")) {
                // Keep internal paragraph breaks, removing only the cell's
                // trailing newlines before the following cell separator.
                while (text.endsWith(QChar('\n'))) text.chop(1);
            } else if (name == QStringLiteral("tr")) {
                text.append(QStringLiteral("\n\n"));
                if (!rowHasCell.isEmpty()) rowHasCell.last() = false;
            } else if (name == QStringLiteral("tbl")) {
                if (!rowHasCell.isEmpty()) rowHasCell.removeLast();
                if (!text.endsWith(QStringLiteral("\n\n"))) text.append(QStringLiteral("\n\n"));
            }
        }
    }
    if (xml.hasError() || !foundDocument || !foundBody) {
        warnings->append(QStringLiteral("DOCX 正文 XML 无效，已放弃部分提取结果：")
                         + (xml.hasError() ? xml.errorString() : QStringLiteral("缺少有效的 document/body 元素")));
        return {};
    }
    result->extractedText = text;
        if (text.trimmed().isEmpty()) {
        warnings->append(QStringLiteral("DOCX 正文和表格没有可读取文字；空白文件或仅含图片的文件需另行处理。"));
    }
    return normalize(text);
}

QString DocumentProcessor::readPdfText(const QString& path, const QString& pdfToText, DocumentResult* result) const
{
    QStringList* warnings = &result->warnings;
    const QString requested = pdfToText.trimmed();
    if (!requested.isEmpty() && !QFileInfo(requested).isFile()) {
        warnings->append(QStringLiteral("指定的 pdftotext 程序不存在：") + requested);
        return {};
    }
    const QString tool = findTool(requested, {QStringLiteral("pdftotext.exe"), QStringLiteral("pdftotext")});
    if (tool.isEmpty()) {
        warnings->append(QStringLiteral("未找到 pdftotext，无法读取 PDF。可安装 Poppler，并在界面中指定 pdftotext.exe。"));
        return {};
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(tool, {QStringLiteral("-layout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), path, QStringLiteral("-")});
    if (!process.waitForStarted(10000)) {
        warnings->append(QStringLiteral("无法启动 pdftotext：") + process.errorString());
        return {};
    }
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished();
        warnings->append(QStringLiteral("PDF 文本提取超时。"));
        return {};
    }
    const QByteArray output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || output.isEmpty()) {
        warnings->append(QStringLiteral("PDF 未提取到文本，可能是扫描件或 PDF 工具执行失败：")
                         + QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return {};
    }
    QString text = QString::fromUtf8(output);
    const QString substantive = substantivePdfText(text);
    if (substantive.size() < 80) {
        warnings->append(QStringLiteral("PDF 提取到的实质文本过少或疑似只有水印/页码；请核对页面，必要时执行 OCR。"));
        if (substantive.isEmpty()) warnings->append(QStringLiteral("PDF 没有可识别的实质文本，需要 OCR 后再处理。"));
    }
    result->extractedText = text;
    return normalize(text);
}

QList<QPair<QString, QString>> DocumentProcessor::splitClauses(const QString& source, bool plainText) const
{
    const QString text = normalize(source);
    QList<QPair<QString, QString>> clauses;
    if (text.isEmpty()) return clauses;

    // TXT article headings must begin a line. Inline references such as
    // “按照第三条规定” belong to the current article.
    const QString articlePattern = plainText
            ? QStringLiteral("^[ \\t]*第[ \\t]*([0-9零〇一二三四五六七八九十百千万两]+)[ \\t]*条")
            : QStringLiteral("第\\s*([0-9零〇一二三四五六七八九十百千万两]+)\\s*条");
    const QRegularExpression articleExpression(articlePattern, QRegularExpression::MultilineOption);
    QList<QRegularExpressionMatch> matches;
    QRegularExpressionMatchIterator iterator = articleExpression.globalMatch(text);
    while (iterator.hasNext()) matches.append(iterator.next());
    if (!matches.isEmpty()) {
        if (matches.first().capturedStart() > 0) {
            const QString preamble = text.left(matches.first().capturedStart()).trimmed();
            if (!preamble.isEmpty()) clauses.append({QString(), preamble});
        }
        for (int index = 0; index < matches.size(); ++index) {
            const qsizetype start = matches[index].capturedStart();
            const qsizetype end = (index + 1 < matches.size()) ? matches[index + 1].capturedStart() : text.size();
            const QString body = text.mid(start, end - start).trimmed();
            clauses.append({QStringLiteral("第") + matches[index].captured(1) + QStringLiteral("条"), body});
        }
        return clauses;
    }

    // Engineering standards commonly use headings such as 1.0.1 and 13.4.1.
    // Normalize full-width digits before matching, while preserving the source text.
    QString headingText = text;
    for (ushort digit = 0; digit < 10; ++digit)
        headingText.replace(QChar(0xFF10 + digit), QChar('0' + digit));
    headingText.replace(QChar(0xFF0E), QChar('.'));
    headingText.replace(QChar(0xFF1A), QChar(':'));
    headingText.replace(QChar(0xFF1B), QChar(';'));
    const QRegularExpression headingExpression(
        QStringLiteral("(?m)^[ \\t]*([0-9]+(?:\\.[0-9]+){1,5})(?=[.． \\t])[.．]?[ \\t]*(?=\\S)[^\\n]*"));
    QList<QRegularExpressionMatch> headingMatches;
    iterator = headingExpression.globalMatch(headingText);
    QList<QRegularExpressionMatch> headingCandidates;
    while (iterator.hasNext()) headingCandidates.append(iterator.next());
    const QRegularExpression tocContinuation(
        QStringLiteral("(?m)^[^\\n]{0,180}(?:\\.{3,}|…{2,}|·{3,}|。{3,})[^\\n]*(?:[（(][0-9０-９]+[）)])?\\s*$|^[ \\t]*[（(][0-9０-９]+[）)]\\s*$"));
    for (int candidateIndex = 0; candidateIndex < headingCandidates.size(); ++candidateIndex) {
        const QRegularExpressionMatch match = headingCandidates[candidateIndex];
        const qsizetype lineEnd = headingText.indexOf(QChar('\n'), match.capturedEnd());
        const qsizetype afterLine = lineEnd < 0 ? headingText.size() : lineEnd + 1;
        const QString line = headingText.mid(match.capturedStart(),
            (lineEnd < 0 ? headingText.size() : lineEnd) - match.capturedStart()).trimmed();
        // Table-of-contents leaders and page-number suffixes are not clause headings.
        if (QRegularExpression(QStringLiteral("(?:\\.{3,}|…{2,}|·{3,}|。{3,}|\\.\\s*\\.\\s*\\.)|(?:\\s*[（(][0-9０-９]+[）)])\\s*$"))
                .match(line).hasMatch()) continue;
        const qsizetype nextCandidate = candidateIndex + 1 < headingCandidates.size()
            ? headingCandidates[candidateIndex + 1].capturedStart() : headingText.size();
        const qsizetype windowEnd = qMin(nextCandidate, afterLine + qsizetype(300));
        const QString followingText = headingText.mid(afterLine, windowEnd - afterLine);
        if (tocContinuation.match(followingText).hasMatch()) continue;
        // Announcements and lists can mention several numbered clauses on one line.
        const QString remainder = line.mid(match.capturedLength(1));
        if (QRegularExpression(QStringLiteral("[0-9０-９]+(?:\\.[0-9０-９]+){1,5}[.．、]"))
                .match(remainder).hasMatch()) continue;
        headingMatches.append(match);
    }
    if (!headingMatches.isEmpty()) {
        // Discard cover, preface and contents before the first verified heading.
        for (int index = 0; index < headingMatches.size(); ++index) {
            const qsizetype start = headingMatches[index].capturedStart();
            const qsizetype end = index + 1 < headingMatches.size() ? headingMatches[index + 1].capturedStart() : text.size();
            clauses.append({headingMatches[index].captured(1), text.mid(start, end - start).trimmed()});
        }
        return clauses;
    }

    const QString paragraphPattern = plainText
            ? QStringLiteral("\\n[ \\t]*\\n|(?<=[。！？；;])\\n")
            : QStringLiteral("\\n\\s*\\n|(?<=。)\\n");
    const QStringList paragraphs = text.split(QRegularExpression(paragraphPattern), Qt::SkipEmptyParts);
    for (const QString& paragraph : paragraphs) {
        const QString value = paragraph.trimmed();
        if (!value.isEmpty()) clauses.append({QString(), value});
    }
    if (!clauses.isEmpty()) return clauses;

    const QRegularExpression sentenceExpression(QStringLiteral("[^。！？；;\\n]{2,300}[。！？；;]?"));
    iterator = sentenceExpression.globalMatch(text);
    while (iterator.hasNext()) {
        const QString sentence = iterator.next().captured(0).trimmed();
        if (sentence.size() >= 20) clauses.append({QString(), sentence});
    }
    if (clauses.isEmpty()) clauses.append({QString(), text});
    return clauses;
}

QList<QString> DocumentProcessor::splitPdfPages(const QString& source) const
{
    // pdftotext emits a form-feed between pages unless -nopgbrk is requested.
    // Keep empty pages so source_page remains aligned with the PDF page number.
    if (!source.contains(QChar('\f'))) return {source};
    QList<QString> pages;
    const QStringList parts = source.split(QChar('\f'), Qt::KeepEmptyParts);
    for (const QString& part : parts) pages.append(part);
    return pages;
}

QStringList DocumentProcessor::writeOutputs(DocumentResult& result, const ProcessorOptions& options) const
{
    QDir outputDir(options.outputDirectory);
    QStringList errors;
    const auto save = [&errors](const QString& path, const QByteArray& data) {
        QString error;
        if (!writeUtf8(path, data, &error)) errors.append(error);
    };
    const QString base = result.outputBase.isEmpty()
            ? outputDir.filePath(QFileInfo(result.sourceFile).completeBaseName())
            : result.outputBase;

    QJsonObject metadata;
    metadata[QStringLiteral("source_file")] = result.sourceFile;
    metadata[QStringLiteral("source_path")] = result.sourcePath;
    metadata[QStringLiteral("file_type")] = result.suffix;
    metadata[QStringLiteral("page_count")] = result.pageCount;
    metadata[QStringLiteral("rule_count")] = result.rules.size();
    metadata[QStringLiteral("library_added")] = result.libraryAdded;
    metadata[QStringLiteral("library_duplicates")] = result.libraryDuplicates;
    metadata[QStringLiteral("library_errors")] = result.libraryErrors;
    metadata[QStringLiteral("skipped_noise")] = result.skippedNoise;
    metadata[QStringLiteral("status")] = result.status;
    metadata[QStringLiteral("output_base")] = base;
    if (!result.sourceEncoding.isEmpty()) {
        metadata[QStringLiteral("source_encoding")] = result.sourceEncoding;
    }
    if (!result.sourceEncoding.isEmpty()
        || ((result.suffix == QStringLiteral("docx") || result.suffix == QStringLiteral("pdf"))
            && !result.extractedText.isEmpty())) {
        metadata[QStringLiteral("extracted_text_file")] = base + QStringLiteral(".extracted.txt");
        save(base + QStringLiteral(".extracted.txt"), result.extractedText.toUtf8());
    }
    if (result.suffix == QStringLiteral("docx")) {
        metadata[QStringLiteral("extraction_scope")] = QStringLiteral("word/document.xml 正文与表格；不包含页眉、页脚、脚注和图片文字");
    } else if (result.suffix == QStringLiteral("pdf")) {
        metadata[QStringLiteral("extraction_scope")] = QStringLiteral("pdftotext -layout 文本层；保留页面分隔符，不包含扫描图片文字");
    }
    metadata[QStringLiteral("processed_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    metadata[QStringLiteral("classification_taxonomy")] = QDir(QFileInfo(options.configPath).absoluteDir()).filePath(
        QStringLiteral("classification/project_categories.json"));
    metadata[QStringLiteral("runtime_package_directory")] = options.packageOutputDirectory;
    metadata[QStringLiteral("warnings")] = QJsonArray::fromStringList(result.warnings);
    QJsonObject categoryNames;
    for (const QString& code : m_engine.categoryCodes()) categoryNames[code] = m_engine.categoryName(code);
    metadata[QStringLiteral("category_names")] = categoryNames;

    QJsonArray rules;
    for (const ClauseRecord& record : result.rules) rules.append(record.toJson());
    QJsonObject root;
    root[QStringLiteral("metadata")] = metadata;
    root[QStringLiteral("rules")] = rules;

    QString csv;
    QTextStream csvStream(&csv);
    csvStream.setEncoding(QStringConverter::Utf8);
    csvStream << QChar(0xFEFF);
    csvStream << "rule_id,source_file,document_code,source_page,source_page_end,article,rule_title,text,applicable_categories,category_names,subcategory_codes,professional_tag_codes,scene_codes,category_confidence,dimensions,dimension_codes,obligation_level,conditions,evidence,matched_keywords,review_flags,priority\n";
    for (const ClauseRecord& record : result.rules) {
        const QStringList fields = {
            record.ruleId, record.sourceFile, record.documentCode, QString::number(record.sourcePage), QString::number(record.sourcePageEnd), record.article, record.title, record.text,
            record.categories.join(QStringLiteral("；")), record.categoryNames.join(QStringLiteral("；")),
            record.subcategories.join(QStringLiteral("；")), record.professionalTagCodes.join(QStringLiteral("；")), record.sceneCodes.join(QStringLiteral("；")), record.categoryConfidence,
            record.dimensions.join(QStringLiteral("；")), record.dimensionCodes.join(QStringLiteral("；")), record.obligationLevel, record.conditions.join(QStringLiteral("；")),
            record.evidence.join(QStringLiteral("；")), record.matchedKeywords.join(QStringLiteral("；")),
            record.reviewFlags.join(QStringLiteral("；")), record.priority
        };
        QStringList escaped;
        for (const QString& field : fields) escaped.append(csvCell(field));
        csvStream << escaped.join(QChar(',')) << '\n';
    }
    save(base + QStringLiteral(".rules.csv"), csv.toUtf8());

    QMap<QString, int> categoryCounts;
    QMap<QString, int> dimensionCounts;
    for (const ClauseRecord& record : result.rules) {
        for (const QString& category : record.categories) categoryCounts[category]++;
        for (const QString& dimension : record.dimensions) dimensionCounts[dimension]++;
    }
    QString report;
    QTextStream md(&report);
    md.setEncoding(QStringConverter::Utf8);
    md << "# 法规标准处理报告：" << result.sourceFile << "\n\n";
    md << "> 状态：" << (result.status.isEmpty() ? QStringLiteral("未标记") : result.status) << "  \n";
    md << "> 页数：" << result.pageCount << "；条款数量：" << result.rules.size() << "  \n";
    md << "> 已跳过噪声：" << result.skippedNoise << " 条  \n";
    md << "> 处理时间：" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\n\n";
    md << "本报告由 C++/Qt 批量处理器生成。分类结果用于规则路由和人工复核，不替代法律适用判断，也不自动改变招标文件中的评标标准。\n\n";
    if (!result.warnings.isEmpty()) {
        md << "## 提取警告\n\n";
        for (const QString& warning : result.warnings) md << "- " << warning << '\n';
        md << '\n';
    }
    md << "## 主类统计\n\n| 主类 | 条款数 |\n|---|---:|\n";
    for (auto it = categoryCounts.cbegin(); it != categoryCounts.cend(); ++it) {
        const QString label = it.key() == QStringLiteral("待判定")
                ? QStringLiteral("待判定（需复核）")
                : it.key() + QChar(' ') + m_engine.categoryName(it.key());
        md << "| " << mdCell(label) << " | " << it.value() << " |\n";
    }
    md << "\n## 规则维度统计\n\n| 维度 | 条款数 |\n|---|---:|\n";
    for (auto it = dimensionCounts.cbegin(); it != dimensionCounts.cend(); ++it) md << "| " << mdCell(it.key()) << " | " << it.value() << " |\n";
    md << "\n## 待人工复核条款\n\n| 编号 | 条款 | 主类 | 维度 | 复核原因 |\n|---|---|---|---|---|\n";
    int flaggedCount = 0;
    for (const ClauseRecord& record : result.rules) {
        if (record.reviewFlags.isEmpty()) continue;
        ++flaggedCount;
        md << "| " << mdCell(record.ruleId) << " | " << mdCell(record.article.isEmpty() ? QStringLiteral("段落") : record.article)
           << " | " << mdCell(record.categories.join(QStringLiteral("、"))) << " | "
           << mdCell(record.dimensions.join(QStringLiteral("、"))) << " | " << mdCell(record.reviewFlags.join(QStringLiteral("；"))) << " |\n";
    }
    if (flaggedCount == 0) md << "| - | - | - | - | 未发现自动复核提示 |\n";
    md << "\n## 条款明细\n\n| 编号 | 页码 | 来源条款 | 主类 | 维度 | 义务强度 | 条款内容 |\n|---|---:|---|---|---|---|---|\n";
    for (const ClauseRecord& record : result.rules) {
        const QString page = record.sourcePage <= 0 ? QStringLiteral("-")
                                                     : (record.sourcePageEnd > record.sourcePage
                                                        ? QStringLiteral("%1-%2").arg(record.sourcePage).arg(record.sourcePageEnd)
                                                        : QString::number(record.sourcePage));
        md << "| " << mdCell(record.ruleId) << " | " << mdCell(page) << " | " << mdCell(record.article.isEmpty() ? QStringLiteral("段落") : record.article)
           << " | " << mdCell(record.categories.join(QStringLiteral("、"))) << " | " << mdCell(record.dimensions.join(QStringLiteral("、")))
           << " | " << mdCell(record.obligationLevel) << " | " << mdCell(record.text) << " |\n";
    }
    save(base + QStringLiteral(".report.md"), report.toUtf8());
    if (!errors.isEmpty()) {
        result.status = QStringLiteral("failed");
        result.warnings.append(errors);
    }
    metadata[QStringLiteral("status")] = result.status;
    metadata[QStringLiteral("warnings")] = QJsonArray::fromStringList(result.warnings);
    root[QStringLiteral("metadata")] = metadata;
    save(base + QStringLiteral(".rules.json"), QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!errors.isEmpty()) result.status = QStringLiteral("failed");
    return errors;
}

BatchResult DocumentProcessor::process(const QStringList& files, const ProcessorOptions& options)
{
    BatchResult batch;
    QString configError;
    if (!m_engine.load(options.configPath, &configError, options.classificationPath)) {
        batch.errors.append(configError);
        return batch;
    }

    QJsonObject categoryNames;
    for (const QString& code : m_engine.categoryCodes()) {
        categoryNames[code] = m_engine.categoryName(code);
    }

    const QString configDirectory = QFileInfo(options.configPath).absoluteDir().absolutePath();
    const QString libraryPath = options.libraryPath.trimmed().isEmpty()
            ? QDir::cleanPath(QDir(configDirectory).filePath(QStringLiteral("../data/rule_library.json")))
            : QDir::cleanPath(options.libraryPath);
    const QString indexDirectory = options.indexDirectory.trimmed().isEmpty()
            ? QDir::cleanPath(QDir(configDirectory).filePath(QStringLiteral("../data/indexes")))
            : QDir::cleanPath(options.indexDirectory);

    QString workspacePathError;
    if (!validateOutputDirectory(libraryPath, &workspacePathError)) {
        batch.errors.append(QStringLiteral("主规则库路径必须位于 D:\\Code\\Codex 下：") + libraryPath);
        return batch;
    }
    if (!validateOutputDirectory(indexDirectory, &workspacePathError)) {
        batch.errors.append(QStringLiteral("规则库索引目录必须位于 D:\\Code\\Codex 下：") + indexDirectory);
        return batch;
    }

    RuleLibraryStore library;
    QString libraryError;
    if (!library.open(libraryPath, indexDirectory, categoryNames, &libraryError)) {
        batch.errors.append(libraryError);
        return batch;
    }
    batch.libraryPath = libraryPath;
    batch.indexDirectory = indexDirectory;

    QString outputError;
    if (!validateOutputDirectory(options.outputDirectory, &outputError)) {
        batch.errors.append(outputError);
        return batch;
    }
    if (!QDir().mkpath(options.outputDirectory)) {
        batch.errors.append(QStringLiteral("无法创建输出目录：") + options.outputDirectory);
        return batch;
    }

    const int total = files.size();
    QSet<QString> usedOutputBases;
    for (int index = 0; index < files.size(); ++index) {
        const QString path = QDir::cleanPath(files[index]);
        emit progress(QStringLiteral("正在处理：") + path, index + 1, total);
        QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            batch.errors.append(QStringLiteral("文件不存在：") + path);
            continue;
        }
        DocumentResult result;
        result.sourceFile = info.fileName();
        result.sourcePath = info.absoluteFilePath();
        result.suffix = info.suffix().toLower();
        result.pageCount = 1;
        result.status = QStringLiteral("success");
        const bool isPlainText = result.suffix == QStringLiteral("txt") || result.suffix == QStringLiteral("md")
                              || result.suffix == QStringLiteral("markdown");

        QString stem = info.completeBaseName();
        QString outputBase = QDir(options.outputDirectory).filePath(stem);
        int duplicate = 1;
        const auto outputExists = [&usedOutputBases](const QString& base) {
            if (usedOutputBases.contains(base.toCaseFolded())) return true;
            for (const QString& extension : {QStringLiteral(".rules.json"), QStringLiteral(".rules.csv"),
                                            QStringLiteral(".report.md"), QStringLiteral(".extracted.txt")}) {
                if (QFileInfo::exists(base + extension)) return true;
            }
            return false;
        };
        while (outputExists(outputBase)) {
            ++duplicate;
            outputBase = QDir(options.outputDirectory).filePath(stem + QStringLiteral("_") + QString::number(duplicate));
        }
        usedOutputBases.insert(outputBase.toCaseFolded());
        result.outputBase = outputBase;

        QString text;
        if (isPlainText) {
            result.pageCount = 0; // Plain text does not contain reliable page numbers.
            text = readPlainText(path, &result);
        } else if (result.suffix == QStringLiteral("docx")) {
            result.pageCount = 0; // XML extraction does not render Word pages.
            text = readDocxText(path, options.sevenZipPath, &result);
        } else if (result.suffix == QStringLiteral("pdf")) {
            text = readPdfText(path, options.pdfToTextPath, &result);
            result.pageCount = 0;
        } else {
            result.warnings.append(QStringLiteral("不支持的文件类型：") + result.suffix);
            result.status = QStringLiteral("failed");
        }

        const PdfTextQuality pdfQuality = result.suffix == QStringLiteral("pdf")
            ? inspectPdfTextQuality(text) : PdfTextQuality{};
        if (pdfQuality.suspiciousEncoding) {
            result.status = QStringLiteral("warning");
            const QString warning = QStringLiteral("疑似编码/字体提取异常，建议换源或 OCR；明显不可读片段已跳过规则库写入。");
            if (!result.warnings.contains(warning)) result.warnings.append(warning);
        }

        if (text.trimmed().isEmpty()) {
            result.warnings.append(QStringLiteral("没有可处理的文本，未生成条款。"));
            if (result.suffix == QStringLiteral("pdf") &&
                !result.warnings.join(QStringLiteral("；")).contains(QStringLiteral("未找到 pdftotext")) &&
                !result.warnings.join(QStringLiteral("；")).contains(QStringLiteral("指定的 pdftotext 程序不存在"))) {
                result.status = QStringLiteral("needs_ocr");
            } else {
                result.status = QStringLiteral("failed");
            }
        } else {
            if (result.suffix == QStringLiteral("pdf") && substantivePdfText(text).size() < 80)
                result.status = QStringLiteral("warning");
            int ordinal = 0;
            const bool isPdf = result.suffix == QStringLiteral("pdf");
            if (isPdf) result.pageCount = qMax(1, text.count(QChar('\f')) + 1);
            const QList<QPair<QString, QString>> clauses = splitClauses(text, isPlainText || result.suffix == QStringLiteral("docx"));
            qsizetype searchOffset = 0;
            for (const auto& clause : clauses) {
                QString clauseText = clause.second;
                const qsizetype found = text.indexOf(clause.second, searchOffset);
                const qsizetype start = found >= 0 ? found : searchOffset;
                const qsizetype end = start + clause.second.size();
                if (found >= 0) searchOffset = end;
                clauseText.replace(QChar('\f'), QChar('\n'));
                clauseText = normalize(clauseText);
                // Keep short clauses for review. A short mandatory prohibition can
                // still be a meaningful legal requirement.
                if (clauseText.trimmed().isEmpty()) continue;
                if (isNoiseClause(clauseText)) {
                    ++result.skippedNoise;
                    continue;
                }
                if (pdfQuality.suspiciousEncoding && isUnreadablePdfFragment(clauseText)) {
                    ++result.skippedNoise;
                    continue;
                }
                ++ordinal;
                const int pageStart = isPdf ? 1 + text.left(start).count(QChar('\f')) : 0;
                const int pageEnd = isPdf ? 1 + text.left(qMax(start, end - 1)).count(QChar('\f')) : 0;
                ClauseRecord record = m_engine.classify(result.sourceFile, pageStart, clause.first, clauseText, ordinal);
                record.sourcePageEnd = pageEnd;
                result.rules.append(record);
            }
            if (result.rules.isEmpty()) result.status = QStringLiteral("warning");
        }
        if (result.status == QStringLiteral("success") && !result.warnings.isEmpty()) result.status = QStringLiteral("warning");

        for (const ClauseRecord& record : result.rules) {
            if (isNoiseClause(record.text)) continue; // Defensive; counted before classification.
            QString insertError;
            const LibraryInsertResult inserted = library.addClause(record, &insertError, false);
            if (inserted.status == LibraryInsertStatus::Added) {
                ++result.libraryAdded;
                ++batch.libraryAdded;
            } else if (inserted.status == LibraryInsertStatus::ExactDuplicate) {
                ++result.libraryDuplicates;
                ++batch.libraryDuplicates;
            } else {
                ++result.libraryErrors;
                ++batch.libraryErrors;
                const QString detail = insertError.isEmpty() ? inserted.message : insertError;
                const QString warning = QStringLiteral("规则库写入失败：") + detail;
                result.warnings.append(warning);
                batch.errors.append(result.sourcePath + QStringLiteral("：") + warning);
            }
        }
        if (result.suffix == QStringLiteral("pdf") && result.skippedNoise > 0 && result.libraryAdded == 0
            && result.libraryDuplicates == 0) {
            result.status = QStringLiteral("warning");
            const QString warning = QStringLiteral("PDF 条款仅包含疑似下载站水印或噪声片段，未写入主规则库；请核对原文或执行 OCR。");
            if (!result.warnings.contains(warning)) result.warnings.append(warning);
        }
        if (result.status == QStringLiteral("success") && !result.warnings.isEmpty()) {
            result.status = QStringLiteral("warning");
        }

        if (result.status == QStringLiteral("failed") || result.status == QStringLiteral("needs_ocr")) {
            batch.errors.append(QStringLiteral("%1：%2").arg(result.sourcePath, result.warnings.join(QStringLiteral("；"))));
        }
        batch.errors.append(writeOutputs(result, options));
        batch.skippedNoise += result.skippedNoise;
        batch.documents.append(result);
        const bool failed = result.status == QStringLiteral("failed") || result.status == QStringLiteral("needs_ocr");
        emit progress(QStringLiteral("%1：%2（%3 条）").arg(failed ? QStringLiteral("未成功") : QStringLiteral("完成"),
                                                        result.sourceFile).arg(result.rules.size()), index + 1, total);
    }

    QJsonArray summaries;
    for (const DocumentResult& result : batch.documents) {
        QJsonObject item;
        item[QStringLiteral("source_file")] = result.sourceFile;
        item[QStringLiteral("source_path")] = result.sourcePath;
        item[QStringLiteral("rule_count")] = result.rules.size();
        item[QStringLiteral("status")] = result.status;
        item[QStringLiteral("output_base")] = result.outputBase;
        item[QStringLiteral("page_count")] = result.pageCount;
        item[QStringLiteral("library_added")] = result.libraryAdded;
        item[QStringLiteral("library_duplicates")] = result.libraryDuplicates;
        item[QStringLiteral("library_errors")] = result.libraryErrors;
        item[QStringLiteral("skipped_noise")] = result.skippedNoise;
        if (!result.sourceEncoding.isEmpty()) item[QStringLiteral("source_encoding")] = result.sourceEncoding;
        item[QStringLiteral("warnings")] = QJsonArray::fromStringList(result.warnings);
        summaries.append(item);
    }
    QJsonObject summary;
    summary[QStringLiteral("processed_at")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    summary[QStringLiteral("documents")] = summaries;
    summary[QStringLiteral("errors")] = QJsonArray::fromStringList(batch.errors);
    QJsonObject librarySummary;
    librarySummary[QStringLiteral("master_path")] = batch.libraryPath;
    librarySummary[QStringLiteral("index_directory")] = batch.indexDirectory;
    librarySummary[QStringLiteral("added")] = batch.libraryAdded;
    librarySummary[QStringLiteral("duplicates")] = batch.libraryDuplicates;
    librarySummary[QStringLiteral("errors")] = batch.libraryErrors;
    librarySummary[QStringLiteral("skipped_noise")] = batch.skippedNoise;
    summary[QStringLiteral("skipped_noise")] = batch.skippedNoise;
    summary[QStringLiteral("rule_library")] = librarySummary;

    if (!library.save(&libraryError)) {
        ++batch.libraryErrors;
        batch.errors.append(libraryError);
        librarySummary[QStringLiteral("errors")] = batch.libraryErrors;
        summary[QStringLiteral("rule_library")] = librarySummary;
        summary[QStringLiteral("errors")] = QJsonArray::fromStringList(batch.errors);
    }

    // Produce a package whose regulation files can be copied into the later
    // scoring application.  This is deliberately done after the canonical
    // master is saved, so the package always reflects the same revision that
    // the UI reports to the user.
    if (libraryError.isEmpty()) {
        const QString taxonomyPath = options.classificationPath.trimmed().isEmpty()
            ? QDir(configDirectory).filePath(QStringLiteral("classification/project_categories.json"))
            : QDir::cleanPath(options.classificationPath);
        const QString packagePath = options.packageOutputDirectory.trimmed().isEmpty()
            ? QDir(configDirectory).filePath(QStringLiteral("../export/inspection-and-review-system"))
            : QDir::cleanPath(options.packageOutputDirectory);
        RegulationExportOptions exportOptions;
        exportOptions.masterPath = libraryPath;
        exportOptions.taxonomyPath = taxonomyPath;
        exportOptions.outputRoot = packagePath;
        exportOptions.includePending = true;
        const RegulationExportResult exported = RegulationPackageExporter::exportPackage(exportOptions);
        batch.packagePath = exported.packageRoot;
        batch.packageExported = exported.exportedRules;
        batch.packageEntries = exported.runtimeEntries;
        batch.packageSkipped = exported.skippedRules;
        batch.packageWarnings = exported.warnings;
        batch.packageErrors = exported.errors;
        for (const QString& exportError : exported.errors) batch.errors.append(QStringLiteral("运行包导出失败：") + exportError);
        summary[QStringLiteral("runtime_package")] = QJsonObject{
            {QStringLiteral("path"), exported.packageRoot},
            {QStringLiteral("manifest"), exported.manifestPath},
            {QStringLiteral("exported_rules"), exported.exportedRules},
            {QStringLiteral("runtime_entries"), exported.runtimeEntries},
            {QStringLiteral("pending_rules"), exported.skippedRules},
            {QStringLiteral("warnings"), QJsonArray::fromStringList(exported.warnings)},
            {QStringLiteral("errors"), QJsonArray::fromStringList(exported.errors)}
        };
        summary[QStringLiteral("errors")] = QJsonArray::fromStringList(batch.errors);
    }
    if (!writeUtf8(QDir(options.outputDirectory).filePath(QStringLiteral("batch.summary.json")),
                   QJsonDocument(summary).toJson(QJsonDocument::Indented), &outputError)) {
        batch.errors.append(outputError);
    }
    return batch;
}
