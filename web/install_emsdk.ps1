# 安装 Emscripten SDK（后台脚本）。日志: C:\emsdk_install.log
# 注意：这台机器上 `python` 是 Microsoft Store 的假壳（exit 9009），必须用真实路径
$ErrorActionPreference = 'Continue'
$log = 'C:\emsdk_install.log'
$root = 'C:\emsdk'
# 用 py 启动器定位真实 python（PATH 里的 python 可能是 Microsoft Store 假壳）
$py = Split-Path (py -3 -c "import sys;print(sys.executable)" 2>$null)
if (-not $py) { $py = Split-Path (python -c "import sys;print(sys.executable)" 2>$null) }
$env:PATH = "$py;" + $env:PATH

if (-not (Test-Path $root)) {
    "== git clone emsdk ==" | Tee-Object -FilePath $log -Append
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git $root *>&1 |
        Tee-Object -FilePath $log -Append
}

Set-Location $root
"== python: $py ==" | Tee-Object -FilePath $log -Append
"== emsdk.py install latest ==" | Tee-Object -FilePath $log -Append
& "$py\python.exe" "$root\emsdk.py" install latest *>&1 | Tee-Object -FilePath $log -Append

"== emsdk.py activate latest ==" | Tee-Object -FilePath $log -Append
& "$py\python.exe" "$root\emsdk.py" activate latest *>&1 | Tee-Object -FilePath $log -Append

"== done. emcc exists? ==" | Tee-Object -FilePath $log -Append
(Test-Path "$root\upstream\emscripten\emcc.bat") | Tee-Object -FilePath $log -Append
