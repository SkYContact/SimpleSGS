@echo off
rem 定向复现构建 -> build\test_axe.exe
setlocal
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\t mkdir build\t
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /MD /Isrc /ID:\inferfarm\include /Fo:build\t\ /Fe:build\test_axe.exe tools\test_axe.cpp src\AiAgent.cpp src\CardDatabase.cpp src\CardEffects.cpp src\ConsoleAgent.cpp src\Room.cpp src\Types.cpp src\features.cpp src\record_agent.cpp src\nn_agent.cpp src\sgs_farm.cpp /link D:\inferfarm\build\Release\inferfarm.lib
if errorlevel 1 exit /b 1
echo [ok] build\test_axe.exe
