@echo off
chcp 65001 >nul
set "PROJECT=D:\Code\Codex\标书评分项目"
set "APP=%PROJECT%\bin\RegulationBatchProcessor.exe"
set "APP_DIR=%PROJECT%\bin"
set "QT_ROOT=D:\software\QT\6.5.3\mingw_64"
set "QT_TOOLS=D:\software\QT\Tools"
set "PATH=%QT_ROOT%\bin;%QT_TOOLS%\mingw1120_64\bin;%PATH%"

if not exist "%APP%" (
  echo 找不到程序，请先运行 编译C++法规处理器.cmd
  pause
  exit /b 1
)

"%QT_ROOT%\bin\windeployqt.exe" --compiler-runtime --no-translations --qtpaths "%QT_ROOT%\bin\qtpaths.exe" "%APP%"

rem 当前 Qt 安装的 windeployqt 可能无法自动定位平台插件，下面是可重复执行的兜底复制。
if not exist "%APP_DIR%\Qt6Core.dll" copy /Y "%QT_ROOT%\bin\Qt6Core.dll" "%APP_DIR%\Qt6Core.dll" >nul
if not exist "%APP_DIR%\Qt6Gui.dll" copy /Y "%QT_ROOT%\bin\Qt6Gui.dll" "%APP_DIR%\Qt6Gui.dll" >nul
if not exist "%APP_DIR%\Qt6Widgets.dll" copy /Y "%QT_ROOT%\bin\Qt6Widgets.dll" "%APP_DIR%\Qt6Widgets.dll" >nul
if not exist "%APP_DIR%\libgcc_s_seh-1.dll" copy /Y "%QT_TOOLS%\mingw1120_64\bin\libgcc_s_seh-1.dll" "%APP_DIR%\libgcc_s_seh-1.dll" >nul
if not exist "%APP_DIR%\libstdc++-6.dll" copy /Y "%QT_TOOLS%\mingw1120_64\bin\libstdc++-6.dll" "%APP_DIR%\libstdc++-6.dll" >nul
if not exist "%APP_DIR%\libwinpthread-1.dll" copy /Y "%QT_TOOLS%\mingw1120_64\bin\libwinpthread-1.dll" "%APP_DIR%\libwinpthread-1.dll" >nul
if not exist "%APP_DIR%\platforms" mkdir "%APP_DIR%\platforms"
rem windeployqt 在含中文路径时可能漏复制平台插件，统一用当前 Qt 安装覆盖部署。
for %%P in (qwindows.dll qoffscreen.dll qminimal.dll qdirect2d.dll) do (
  if exist "%QT_ROOT%\plugins\platforms\%%P" copy /Y "%QT_ROOT%\plugins\platforms\%%P" "%APP_DIR%\platforms\%%P" >nul
)

rem 让直接双击和批处理启动都优先使用程序目录中的平台插件。
>"%APP_DIR%\qt.conf" echo [Paths]
>>"%APP_DIR%\qt.conf" echo Plugins=.

echo 部署完成：%APP%
pause
