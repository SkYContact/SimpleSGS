@echo off
rem ===========================================================================
rem  用 Emscripten 把 C++ 内核编成 WebAssembly，输出到 web\dist\
rem  需要先装好 emsdk（见 install_emsdk.ps1），默认装在 C:\emsdk
rem ===========================================================================
setlocal
set EMSDK_QUIET=1
if not defined EMSDK if exist "%~dp0..\build\emsdk\emsdk_env.bat" set "EMSDK=%~dp0..\build\emsdk"
if not defined EMSDK set EMSDK=C:\emsdk

rem PATH 里的 python 可能是 Microsoft Store 的假壳（跑起来 exit 9009），
rem 用 py 启动器定位真实解释器后放到 PATH 最前面。
set "REALPY="
for /f "delims=" %%i in ('py -3 -c "import sys,os;print(os.path.dirname(sys.executable))" 2^>nul') do set "REALPY=%%i"
if defined REALPY set "PATH=%REALPY%;%PATH%"

if not exist "%EMSDK%\emsdk_env.bat" (
    echo [x] 找不到 %EMSDK%\emsdk_env.bat，请先运行 install_emsdk.ps1 或设置环境变量 EMSDK
    exit /b 1
)
call "%EMSDK%\emsdk_env.bat" >nul

cd /d "%~dp0.."
if not exist web\dist mkdir web\dist

rem 必须先删旧产物：binaryen 的 --asyncify pass 是"就地改写" sgs.wasm，
rem 文件若还被浏览器预览/静态服务器占着，wasm-opt 会直接崩（0xC0000005）。
del /q /f web\dist\sgs.wasm web\dist\sgs.js web\dist\sgs.data 2>nul
if exist web\dist\sgs.wasm (
    echo [x] web\dist\sgs.wasm 删不掉，说明还被占用：
    echo     先关掉浏览器预览页面 / 停掉 serve.bat，再重新编译。
    exit /b 1
)

echo === em++ 编译中（第一次会慢一些）===
rem 注意：这里必须用 -O1。用 -O2/-O3 时 binaryen 的 --asyncify pass 会报
rem       "Module::getGlobal: __asyncify_state does not exist"（本 emsdk 的 binaryen 与
rem       --strip-target-features 组合有 bug）。-O1 已验证可用。
em++ -O1 -std=c++17 -I src ^
    src/Types.cpp src/CardDatabase.cpp src/Room.cpp src/CardEffects.cpp src/AiAgent.cpp ^
    web/wasm_api.cpp ^
    --preload-file data/standard_cards.csv@data/standard_cards.csv ^
    --no-entry ^
    -sASYNCIFY=1 ^
    -sALLOW_MEMORY_GROWTH=1 ^
    -sMODULARIZE=1 ^
    -sEXPORT_NAME=createSgsModule ^
    -sENVIRONMENT=web,node ^
    -sSTACK_SIZE=1048576 ^
    -o web/dist/sgs.js
if errorlevel 1 (
    echo [x] 编译失败
    exit /b 1
)

echo.
echo [ok] 生成:
dir /b web\dist
echo.
echo 本地预览: 运行 web\serve.bat   然后打开 http://localhost:8000
