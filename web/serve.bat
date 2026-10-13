@echo off
rem 起本地静态服务器（wasm 必须走 http，file:// 会被浏览器拦）
rem 用 serve.py 而不是 python -m http.server —— 前者会发 Cache-Control: no-store，
rem 免得改了 app.js / sgs.wasm 之后浏览器还在用旧缓存，看着像"改了没反应"。
setlocal
set "REALPY="
for /f "delims=" %%i in ('py -3 -c "import sys,os;print(os.path.dirname(sys.executable))" 2^>nul') do set "REALPY=%%i"
if defined REALPY set "PATH=%REALPY%;%PATH%"
cd /d "%~dp0.."
echo 打开浏览器访问: http://localhost:8000   (Ctrl+Shift+R 可强制刷新)
python web\serve.py 8000
