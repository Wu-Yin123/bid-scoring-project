# Git 迁移与换电脑操作说明

## 1. 当前仓库内容

首个版本保留源代码、Qt `.ui` 文件、九大类配置、分类索引、规则处理器程序和项目文档。

`output_cpp/` 和 `export/` 是批量处理后可以重新生成的结果，已加入忽略规则；`data/rule_library.json` 通过 Git LFS 保存，避免普通 Git 仓库超过单文件限制。

## 2. 在当前电脑完成首次上传

在项目目录打开 PowerShell：

```powershell
cd D:\Code\Codex\标书评分项目
git config user.name "你的姓名或 Git 用户名"
git config user.email "你的 Git 邮箱"
git branch -M main
git remote add origin <远程仓库地址>
git commit -m "建立法规标准批量处理器当前版本"
git push -u origin main
```

Git LFS 已在本地启用，推送时会同时上传 `data/rule_library.json` 的 LFS 内容。

## 3. 换电脑后的恢复

新电脑先安装 Git、Git LFS、Qt 6.5.3、`pdftotext.exe` 和 7-Zip，然后执行：

```powershell
git lfs install
git clone <远程仓库地址>
cd 标书评分项目
git lfs pull
```

如果新电脑的 Qt 或工具路径不同，需要修改 `编译C++法规处理器.cmd`、`部署C++法规处理器.cmd` 中的依赖路径；CMake 和 qmake 工程中的 UI 路径也应按新电脑目录调整。

## 4. 日常保存进度

```powershell
git add -A
git commit -m "说明本次修改内容"
git push
```

不要把原始法规 PDF、批量输出目录或临时编译目录直接加入仓库；需要备份时单独保存到受控的 D 盘归档目录或发布附件。
