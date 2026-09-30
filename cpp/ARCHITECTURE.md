# C++/Qt 框架说明

## 第一阶段的边界

第一阶段先把流程跑通：批量选择文件、提取文本、切分条款、调用分类规则、输出结构化结果。每个环节都独立，后续可以单独替换，不需要重写整个程序。

## 模块职责

### `MainWindow`

负责文件列表、输出目录、工具路径、进度、日志和结果提示。它不负责解析法规，也不直接写规则字段。

### `RuleLibraryDialog`

负责主规则库和 P01—P09 分类索引的查看界面。窗口以只读方式打开 `RuleLibraryStore`，提供关键字搜索、主类/维度/状态筛选，以及规则原文、来源、分类和机器依据详情。批处理完成后由 `RegulationPackageExporter` 自动生成运行包；人工复核仍保留独立结果文件。

### `RuleLibraryStore`

负责 `data/rule_library.json` 和 `data/indexes/P01.json`—`P09.json` 的读写。主规则库以规范化正文的 SHA-256 `content_hash` 做精确去重，重复条款合并来源并更新对应索引；相似条款不自动合并。

### `DocumentProcessor`

负责批量调度和文件类型适配：

- TXT、Markdown 直接读取；
- DOCX 调用 7-Zip 提取 XML；
- PDF 调用 `pdftotext.exe`；
- 将原文拆成条款；
- 调用 `RuleEngine`；
- 写出 JSON、CSV、Markdown；
- 保存主规则库后生成后续评分项目可读取的运行配置包。

### `RuleEngine`

负责把一条条款映射到：

- P01—P09；
- P01—P09 下的 59 个二级类型；
- 专业标签和施工场景；
- 施工组织、质量、安全、进度、资源、环境、合同招投标；
- 必须/禁止、应当、条件性、可以/建议；
- 适用条件、可核查证据和人工复核提示。

### `models`

定义 `ClauseRecord`、`DocumentResult` 和 `BatchResult`，保证界面、处理器和输出格式使用同一套数据结构。

### `rules.json`

保存分类词库和规则维度词库。调整第一阶段分类时优先修改此文件，不直接修改 C++ 代码。

### `project_categories.json`

保存九大类的边界规则、59 个二级类型、专业标签、施工场景、默认维度和目标包引用。`RuleEngine` 会在存在该文件时自动加载；缺少该文件时仍保持旧版 P01—P09 分类兼容。

### `RegulationPackageExporter`

读取 `data/rule_library.json`，按文档编号聚合条款，输出根节点为 `regulations` 的运行文件，并附带分类目录、维度映射、主库快照、JSON Schema 和 `package_manifest.json`。运行包只写入 D:\Code\Codex 下的目录，不修改后续 ZIP 工程。

## 后续扩展位置

- OCR：在 `DocumentProcessor::readPdfText` 增加扫描件识别分支；
- 页码恢复：把 PDF 提取改为按页调用工具；
- 条款版本管理：在 `ClauseRecord` 增加法规版本、生效日期、废止关系；
- 人工复核：在 `MainWindow` 增加条款编辑表格，并把修改结果保存到修正规则文件；
- 语义分类：在 `RuleEngine` 外增加语义检索层，保留关键词结果作为可解释依据。
