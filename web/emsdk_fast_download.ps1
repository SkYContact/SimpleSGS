# ============================================================================
#  Fast / resumable download of the emsdk toolchain (wasm-binaries.zip ~668MB)
#  then let emsdk unpack it (EMSDK_KEEP_DOWNLOADS=1) and activate the SDK.
#
#  Why: the file only lives on storage.googleapis.com (no CN mirror exists -
#       npmmirror / tencent / tuna / aliyun all return 404).
#       Single connection ~0.8MB/s (~14 min). We use 8 parallel curl ranges.
#
#  Do NOT use `curl -C -` on an emsdk partial file: that server may ignore the
#  Range header and append the whole body, producing a bigger but corrupt file.
#
#  NOTE: this file is intentionally ASCII-only. Windows PowerShell 5.1 reads
#        .ps1 as ANSI (GBK here); UTF-8 Chinese inside strings can swallow a
#        quote character and break parsing.
#
#  Usage: powershell -ExecutionPolicy Bypass -File web\emsdk_fast_download.ps1
#  Log  : C:\emsdk_fast.log
# ============================================================================
$ErrorActionPreference = 'Continue'
$log = 'C:\emsdk_fast.log'
function L($m) { $m | Tee-Object -FilePath $log -Append }

$hash = '6e9f6885cb17a3d6c8fd488a5cff730f50aba61d'   # emsdk 6.0.12 (latest)
$url = "https://storage.googleapis.com/webassembly/emscripten-releases-builds/win/$hash/wasm-binaries.zip"
$expected = 700727317
$out = "C:\emsdk\downloads\$hash-wasm-binaries.zip"
$tmp = 'C:\emsdk\downloads\parts'
$N = 8
# locate the real python (the python on PATH may be the Microsoft Store stub)
$pyDir = Split-Path (py -3 -c "import sys;print(sys.executable)" 2>$null)
if (-not $pyDir) { $pyDir = Split-Path (python -c "import sys;print(sys.executable)" 2>$null) }
$py = "$pyDir\python.exe"
$env:PATH = "$pyDir;" + $env:PATH

# ---------- 0) stop a running emsdk install (never kill this process) ----------
Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
    Where-Object { $_.CommandLine -like '*emsdk.py*' } |
    ForEach-Object { L ("[i] stop python PID " + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
    Where-Object { $_.CommandLine -like '*emsdk_install*' } |
    ForEach-Object { L ("[i] stop powershell PID " + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2

$haveParts = (Get-ChildItem $tmp -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0
$haveFile = (Test-Path $out) -and ((Get-Item $out).Length -eq $expected)

# ---------- 1) merge already downloaded parts, or download them ----------
if (-not $haveFile) {
    if (-not $haveParts) {
        New-Item -ItemType Directory -Force $tmp | Out-Null
        Remove-Item $out -Force -ErrorAction SilentlyContinue
        $chunk = [math]::Ceiling($expected / $N)
        $jobs = @()
        for ($i = 0; $i -lt $N; $i++) {
            $from = $i * $chunk
            $to = [math]::Min($from + $chunk - 1, $expected - 1)
            if ($from -gt $to) { continue }
            $jobs += [pscustomobject]@{
                i = $i; part = "$tmp\part$i"; size = $to - $from + 1
                proc = Start-Process -PassThru -WindowStyle Hidden -FilePath 'C:\Windows\System32\curl.exe' `
                    -ArgumentList @('-sL', '--fail', '--retry', '8', '--retry-delay', '3',
                                    '--connect-timeout', '30', '-r', "$from-$to", '-o', "$tmp\part$i", $url)
            }
        }
        L "[i] downloading $N ranges in parallel ..."
        $jobs | ForEach-Object { $_.proc | Wait-Process }
        $bad = 0
        foreach ($j in $jobs) {
            $len = if (Test-Path $j.part) { (Get-Item $j.part).Length } else { -1 }
            if ($len -ne $j.size) { L ("[x] part " + $j.i + " size " + $len + " != " + $j.size); $bad++ }
        }
        if ($bad -gt 0) { L '[x] some parts incomplete, just run this script again'; exit 1 }
    } else {
        L '[i] parts already on disk, skipping download'
    }

    L '[i] merging parts ...'
    $fs = [System.IO.File]::Create($out)
    try {
        for ($i = 0; $i -lt $N; $i++) {
            $p = "$tmp\part$i"
            if (-not (Test-Path $p)) { continue }
            $fsIn = [System.IO.File]::OpenRead($p)
            $fsIn.CopyTo($fs)
            $fsIn.Close()
        }
    } finally { $fs.Close() }
    Remove-Item "$tmp\*" -Force -ErrorAction SilentlyContinue
}

$got = (Get-Item $out).Length
L ("[i] merged size: " + $got + " / " + $expected)
if ($got -ne $expected) { L '[x] size mismatch'; exit 1 }

# ---------- 2) let emsdk reuse the file, then install & activate ----------
$env:EMSDK_KEEP_DOWNLOADS = '1'
L '== emsdk.py install latest =='
& $py 'C:\emsdk\emsdk.py' install latest *>&1 | Tee-Object -FilePath $log -Append
L '== emsdk.py activate latest =='
& $py 'C:\emsdk\emsdk.py' activate latest *>&1 | Tee-Object -FilePath $log -Append
L ("== emcc exists: " + (Test-Path 'C:\emsdk\upstream\emscripten\emcc.bat'))
