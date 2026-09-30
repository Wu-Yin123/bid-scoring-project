@echo off
chcp 65001 >nul
set "APP_DIR=D:\Code\Codex\标书评分项目\bin"
set "APP=%APP_DIR%\RegulationBatchProcessor.exe"

if not exist "%APP%" (
  echo 找不到程序：%APP%
  pause
  exit /b 1
)

rem 固定使用随程序部署的 Qt 平台插件，避免系统 Qt_PLUGIN_PATH 干扰启动。
set "QT_PLUGIN_PATH="
set "QT_QPA_PLATFORM_PLUGIN_PATH=%APP_DIR%\platforms"
start "" /d "%APP_DIR%" "%APP%"