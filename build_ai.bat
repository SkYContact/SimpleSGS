@echo off
rem 本地 AI 线构建（不入公共版本库）：核心引擎(src，除 main.cpp) + ai/ 管线
rem -> build\sgs1v1.exe（演化脚本默认引擎路径）
setlocal
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /Z7 /MD /Isrc /Iai /ID:\inferfarm\include ^
  /Fo:build\ /Fe:build\sgs1v1.exe ^
  src\Types.cpp src\CardDatabase.cpp src\Room.cpp src\CardEffects.cpp src\AiAgent.cpp ^
  src\ConsoleAgent.cpp src\CardDatabase.cpp src\Types.cpp ^
  ai\features.cpp ai\record_agent.cpp ai\nn_agent.cpp ai\sgs_farm.cpp ai\main_ai.cpp ^
  /link /MAP:build\sgs1v1.map D:\inferfarm\build\Release\inferfarm.lib
if errorlevel 1 exit /b 1
echo [ok] build\sgs1v1.exe (AI line)
