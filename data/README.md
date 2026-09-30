# 规则库数据文件

## 主规则库

`rule_library.json` 保存完整规则。每条规则只保存一份，主要字段如下：

- `rule_id`：主规则库稳定编号，格式为 `RL-...`；
- `content_hash`：规范化正文的 SHA-256，用于精确去重；
- `text`、`title`：规则正文和标题；
- `categories`：适用的 P01—P09，可包含多个类别；
- `subcategory_codes`：P01-01—P09-08 中命中的二级类型；
- `professional_tag_codes`、`scene_codes`：专业标签和施工场景；
- `dimensions`：施工组织、质量、安全等规则维度；
- `dimension_codes`：与目标评分配置对接的英文维度代码；
- `document_code`、`document_title`、`document_level`：文档级元数据（可由人工补充）；
- `sources`：法规名称、页码、条款号和原始规则编号；
- `status`：当前使用状态，初始为 `pending_review`；
- `version`、`first_seen_at`、`last_seen_at`：版本和时间信息；
- `similar_rule_ids`：疑似相似规则编号，后续人工复核使用。

## 分类索引

`indexes/P01.json` 至 `indexes/P09.json` 保存 `rule_ids`，并按 `subcategory_rule_ids`、`professional_tag_rule_ids`、`scene_rule_ids` 提供二级索引，不复制规则正文。检索某个分类时，先读取索引中的编号，再到主规则库读取完整规则。

一条规则可以同时出现在多个索引中，但主规则库中仍只有一份正文。完全相同的正文会合并来源；相似但无法确认相同的正文暂不自动合并。

批处理程序每次启动时只打开一份主规则库，在批次结束时统一保存。首次处理某条正文会计入“新增”，同一批次或后续批次再次遇到相同规范化正文会计入“精确重复”，并补充尚未记录的来源。批次统计写入输出目录的 `batch.summary.json`，规则库写入失败会列入错误和失败计数。

主库保存后，程序还会生成 `D:\Code\Codex\标书评分项目\export\inspection-and-review-system`。该目录中的 `config/regulations/*.json` 根节点为 `regulations`，是后续评分项目的直接输入；`package_manifest.json` 记录源主库、输出文件、运行字段和待复核数量。
