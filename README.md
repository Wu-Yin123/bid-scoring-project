# 工程建设项目法规标准批量处理器（C++/Qt）

这是施工组织设计评审系统的第一阶段框架。程序采用 C++ 和 Qt Widgets 编写，支持批量选择法规、标准和招标文件，逐个提取条款并输出结构化结果。

## 当前框架

```text
用户选择多个文件
      ↓
Qt 图形界面（文件队列、输出目录、进度和日志）
      ↓
DocumentProcessor（按文件类型提取文本）
      ↓
文本规范化、质量检查与条款切分（第 X 条 / 工程标准数字条款 / 段落）
      ↓
RuleEngine（P01-P09 主类 → 子类/标签/场景 → 横向维度）
      ↓
RuleLibraryStore（主库 + 多层索引，按 content_hash 精确去重）
      ↓
JSON + CSV + Markdown 报告
      ↓
目标项目运行配置包（后续重构的输入）
```

处理时必须区分四层信息：P01—P09 是按工程对象和场景划分的主类；59 个子类是主类内部的进一步细分；专业标签和施工场景是补充检索信息；施工组织、质量、安全、进度、资源、环境、合同招投标是可以跨主类复用的横向评审维度。软件以条款为单位提取和分类，不把整本法规直接归成一个类别。

当前版本的检索范围是用户提供的本地 PDF、DOCX、TXT 和 Markdown 文件。程序负责提取文件内容并按配置规则匹配，不负责从互联网或法规数据库自动下载法规；来源、版本和有效性仍需人工核验。

## 当前支持的文件

- TXT、Markdown：直接读取；
- DOCX：通过 7-Zip 提取 `word/document.xml`，支持正文和表格单元格文字；
- PDF：通过 Poppler 的 `pdftotext.exe` 提取文本；扫描型 PDF 后续接 OCR；
- 可一次选择多个文件，也可以递归添加整个文件夹。

PDF 处理会先做文本质量检查。工程标准常见的 `1.0.1`、`13.4.1` 等数字条款标题（包括全角数字和标点）可以被识别；目录页码线、页码、水印和仅含符号的片段会被跳过，不写入规则库。PDF 实质文本过少、疑似只有水印或疑似字体/编码异常时，文件会标记为 `warning`，并在报告中给出“建议换源或 OCR”的提示。

## 项目文件

- `cpp/mainwindow.*`：Qt 图形界面；
- `ui/mainwindow.ui`：Qt Designer 主窗口布局；
- `ui/reviewdialog.ui`：Qt Designer 人工复核窗口布局；
- `ui/rulelibrarydialog.ui`：Qt Designer 规则库管理窗口布局；
- `cpp/document_processor.*`：批量处理和文件提取；
- `cpp/rule_engine.*`：分类和规则标签；
- `cpp/rule_library.*`：主规则库读写、精确去重和九个索引更新；
- `cpp/regulation_package_exporter.*`：把主规则库转换为后续评分项目可直接读取的 `regulations` 运行包；
- `cpp/models.*`：结构化结果模型；
- `cpp/batch_worker.*`：批处理线程工作对象；
- `cpp/rule_library_dialog.*`：规则库管理窗口，支持主库加载、搜索、P01—P09/维度/状态筛选和详情查看；
- `config/rules.json`：P01—P09 和规则维度词库；
- `config/classification/project_categories.json`：P01—P09、59 个二级类型、专业标签、施工场景、边界规则和包引用；
- `config/classification/project_profiles.json`：供后续 `ProjectProfile` 路由读取的九类项目配置；
- `config/regulation_schema/`：主库与运行包字段契约；
- `config/mappings/`：维度到目标评分条目的映射；
- `data/rule_library.json`：主规则库，规则正文只保存一份；
- `data/indexes/P01.json`—`P09.json`：九个分类索引，只保存主规则库编号；
- `bin/RegulationBatchProcessor.exe`：编译后的程序；
- `export/inspection-and-review-system/`：每次批处理自动生成的目标项目兼容运行配置包；
- `软件处理逻辑与操作手册.md`：面向操作人员的处理逻辑、操作步骤和人工测试清单；
- `标书评分项目阶段性工作汇报.md`：当前阶段完成情况、验证结果、边界和下一步计划；
- `验证记录.md`：编译、启动、运行包契约和目标加载器兼容性验证结果；
- `cpp/CMakeLists.txt`：CMake 工程；
- `cpp/RegulationBatchProcessor.pro`：QMake 工程。

## 启动程序

双击：

```text
D:\Code\Codex\标书评分项目\启动C++法规处理器.cmd
```

也可以直接运行：

```text
D:\Code\Codex\标书评分项目\bin\RegulationBatchProcessor.exe
```

界面中按以下顺序操作：

1. 添加多个法规或标准文件，或者添加一个文件夹；
2. 确认分类规则文件 `config\rules.json`；
3. 选择输出目录；
4. 确认“重构运行包”目录（默认 `D:\Code\Codex\标书评分项目\export\inspection-and-review-system`）；
5. 如果处理 DOCX，确认 `7z.exe` 路径；
6. 如果处理 PDF，确认 `pdftotext.exe` 路径；
7. 点击“开始批量处理”。

每个输入文件会生成：

- `文件名.rules.json`：完整结构化规则；
- `文件名.rules.csv`：便于 Excel 复核；
- `文件名.report.md`：便于 Typora 查看；
- TXT、Markdown 还会生成 `文件名.extracted.txt`：统一转换为 UTF-8，便于核对原文；
- DOCX 在提取到正文后会生成 `文件名.extracted.txt`：便于核对正文和表格文字；
- `batch.summary.json`：本批次汇总。

批处理完成后会同时追加 `data/rule_library.json` 和 `data/indexes/P01.json`—`P09.json`。批次汇总中的 `rule_library` 节点，以及主窗口日志，会显示新增、精确重复、跳过噪声和写入失败数量。完全相同的正文按规范化文本的 `content_hash` 合并来源；相似但不能确认相同的条款暂不自动合并。`batch.summary.json` 顶层和 `rule_library` 节点都包含 `skipped_noise`，每个文件的 `.rules.json` 元数据也记录该文件跳过的噪声数量。

批处理完成后还会更新“重构运行包”。运行包的每个法规文件根节点都是 `regulations`，每条记录至少包含目标评分程序当前加载器要求的 `code`、`title`、`issued_by`、`level`、`is_mandatory`、`applies_to_items`、`applies_to_factors`、`keywords`、`scope_clause`、`requirement_text` 和 `non_comply_action`。扩展字段保留 P01—P09、59 个子类、专业标签、施工场景、来源规则编号和复核状态，因此后续重构可以直接读取，也可以继续细化。

运行包目录包括：

- `config/regulations/common.json`：所有已导出的法规；
- `config/regulations/municipal_road.json` 等：按现有项目类型拆分的法规包；
- `config/regulations/integrated_facilities.json`、`highway.json`：P08、P09 的前向兼容包，待后续 ProjectProfile 路由接入；
- `config/classification/project_categories.json`：分类目录；
- `config/classification/project_profiles.json`：项目类型路由配置；
- `data/regulation_master.json`：主库快照；
- `package_manifest.json`：版本、数量、警告和字段契约。

自动抽取条款会以 `review_status=pending_review` 导出，保证新文件处理后能够立即形成可读取的包，同时在后续人工复核时保留风险提示。法规的强制性和不合规处置不会由“必须/应当”等条款语气自动推断；没有明确元数据时，导出为 `is_mandatory=false`、`non_comply_action=warning_only`，避免把机器推断直接变成评分封顶规则。

TXT 读取会识别 UTF-8、UTF-16、UTF-32 的 BOM；无 BOM 的中文文本会尝试按 GB18030/GBK 读取，并在报告中标记为“warning”。法律法规优先以行首“第 X 条”作为条款编号；没有该结构时，工程标准可按行首 `1.0.1`、`13.4.1` 等数字标题切分，正文中的“按照第三条规定”等引用不会被误切成新条款。短条款会保留，并在规则结果中提示人工复核；目录、页码、水印等无实质内容片段会在分类前跳过。

DOCX 当前读取正文和表格，不读取页眉、页脚、脚注、图片中的文字和文本框中的文字。遇到修订记录、损坏 XML 或仅含图片的文件，报告会标记 warning 或 failed，并保留具体原因。

主窗口和人工复核窗口布局保存在 `ui/` 目录，界面控件由 Qt Designer 文件生成，按钮行为仍由对应的 C++ 类负责。

点击主窗口的“规则库管理”后，窗口会直接读取 `data/rule_library.json`。左侧可以按规则编号、标题或正文搜索，也可以按 P01—P09、规则维度和状态筛选；选中规则后，右侧显示原文、来源、分类索引、机器命中依据和元数据。点击“刷新”可以读取批处理刚写入的最新内容。

规则库数据层使用 `content_hash` 对规范化正文做精确去重。重复条款会合并到同一条主规则，并把不同来源追加到 `sources` 数组；一条规则可以同时出现在多个 P01—P09 索引中。相似但不能确认完全相同的条款暂不自动合并，后续由人工复核阶段处理。

PDF 当前通过 `pdftotext -layout` 读取文字层，并生成 `文件名.extracted.txt`。结构化结果会记录 `page_count`、`source_page` 和 `source_page_end`，跨页条款会保留起止页码。指定的 PDF 工具不存在或 PDF 没有可读文本时，批次会标记 failed 或 needs_ocr，不会当作成功处理；提取文本疑似字体/编码异常时会标记 warning，并跳过明显不可读片段。

九大类分类使用 `config\\rules.json` 中的 `core_keywords` 识别候选主类，普通 `keywords` 只作为辅助证据。结构化结果中的 `classification_status` 有三种状态：`classified`（单一候选）、`multiple_candidates`（多个候选）和 `needs_review`（信息不足）。`category_evidence` 会保存 P01-P09 每一类的核心命中、辅助命中、排除词和分数。“待判定”表示当前条款信息不足，不能作为新的类别；多候选表示条款可能同时适用于多个九大类，需要结合项目主要建设对象、工程量、核心工艺、主要风险和评分重点确认项目主类。

## 部署 Qt 运行环境

如果运行时提示 `Could not find the Qt platform plugin "windows"`，双击：

```text
D:\Code\Codex\标书评分项目\部署C++法规处理器.cmd
```

脚本调用 `windeployqt`，并固定补齐 `platforms\qwindows.dll`、`qoffscreen.dll` 等平台插件、Qt DLL 和 MinGW 运行库，同时生成 `bin\qt.conf`。启动脚本会把 Qt 平台插件路径固定到程序目录，避免系统中其它 Qt 环境变量导致插件加载失败。
## 编译

Qt 6.5.3 已安装在 `D:\software\QT\6.5.3`。双击 `D:\Code\Codex\标书评分项目\编译C++法规处理器.cmd` 编译。脚本会把 `cpp` 同步到 D 盘 ASCII 路径 `D:\Code\Codex\regulation_build_src` 再调用 CMake/Ninja，避开 MinGW 对中文源路径的 automoc 限制，编译结果仍复制回项目的 `bin` 目录。所有源文件和编译产物都在 D 盘。

## 分阶段开发计划

### 第一步：批量处理框架

当前已完成：文件队列、批处理线程、法律条款和工程标准数字条款切分、目录/水印噪声过滤、P01—P09 标签、施工/质量/安全标签和三类报告输出。

### 第二步：提高文本提取能力

当前已完成 TXT 编码处理、DOCX 正文与表格提取、PDF 文本页码恢复。后续增加 PDF OCR 和扫描件识别。

### 第三步：完善法规规则库

把每条规则拆成适用条件、义务强度、证据要求、主类、子类、专业标签和施工场景，并支持规则版本更新。当前主库和运行包已经保存这些字段。

### 第四步：九大类分类核对和人工复核

当前已完成核心词与辅助词分层、重复关键词去重、待判定和多候选标记、P01-P09 全量证据输出，以及适用范围排除冲突提示。下一步在界面中查看原文和自动标签，人工修改分类后保存，修正结果可以反哺 `config/rules.json`。

### 第五步：关联招标评分

建立“法规条款—规则条目—招标评审因素—评分说明”的映射表。当前 Qt 软件只生成配置包，不修改后续 ZIP 项目源码；后续重构时直接使用 `export/inspection-and-review-system` 作为输入。

## 当前限制

- PDF 需要安装 Poppler 的 `pdftotext.exe`；
- 扫描型 PDF 需要后续增加 OCR；
- 当前分类采用透明词库规则，复杂交叉条款会标记为需要人工复核；
- 法规条款不会自动改变招标文件规定的评标标准。


