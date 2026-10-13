@echo off
rem 不用 CMake，直接用 MSVC 编译桌面版 -> build\sgs_dbg.exe
rem 先通过 vswhere 自动找 Visual Studio，找不到就设环境变量 VSDIR 手动指定
setlocal
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSDIR set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"

if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [x] 没找到 Visual Studio 的 vcvars64.bat，请设置 VSDIR 环境变量指向 VS 安装目录，
    echo     或者改用 CMake:  cmake -S . -B build ^&^& cmake --build build --config Release
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /Z7 /MD /Isrc /ID:\inferfarm\include /Fo:build\ /Fe:build\sgs_dbg.exe src\*.cpp /link /MAP:build\sgs1v1.map D:\inferfarm\build\Release\inferfarm.lib
if errorlevel 1 exit /b 1
echo [ok] build\sgs_dbg.exe
