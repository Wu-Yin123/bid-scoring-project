@echo off
setlocal
rem Keep the build source on an ASCII D: path.  This avoids a MinGW
rem automoc include limitation when the project directory contains Chinese
rem characters.  The authoritative source remains PROJECT\cpp.
set "PROJECT=D:\Code\Codex\标书评分项目"
set "SOURCE=D:\Code\Codex\regulation_build_src"
set "BUILD=D:\Code\Codex\RegulationBuildAscii2"
set "QT_ROOT=D:\software\QT\6.5.3\mingw_64"
set "QT_TOOLS=D:\software\QT\Tools"
set "PATH=%QT_TOOLS%\mingw1120_64\bin;%QT_TOOLS%\Ninja;%PATH%"

if not exist "%SOURCE%" mkdir "%SOURCE%"
robocopy "%PROJECT%\cpp" "%SOURCE%" /E /XD .qtcreator build bin /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo Failed to synchronize the ASCII build source.
  exit /b 1
)

"%QT_TOOLS%\CMake_64\bin\cmake.exe" -S "%SOURCE%" -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%QT_TOOLS%\Ninja\ninja.exe" -DCMAKE_PREFIX_PATH="%QT_ROOT%" -DCMAKE_CXX_COMPILER="%QT_TOOLS%\mingw1120_64\bin\g++.exe"
if errorlevel 1 exit /b 1
"%QT_TOOLS%\CMake_64\bin\cmake.exe" --build "%BUILD%" --parallel 4
if errorlevel 1 exit /b 1
copy /Y "%BUILD%\bin\RegulationBatchProcessor.exe" "%PROJECT%\bin\RegulationBatchProcessor.exe" >nul
if errorlevel 1 exit /b 1

echo Build complete: %PROJECT%\bin\RegulationBatchProcessor.exe
endlocal
