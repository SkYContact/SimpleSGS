@echo off
rem 调试版（/MDd + 逐分配堆校验入口在 main.cpp 的 _CrtSetDbgFlag）-> build\d_sgs1v1.exe
setlocal
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSDIR set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\d mkdir build\d
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /MDd /Zi /Isrc /Fo:build\d\ /Fe:build\d_sgs1v1.exe src\*.cpp /link /MAP:build\d_sgs1v1.map
if errorlevel 1 exit /b 1
echo [ok] build\d_sgs1v1.exe
