# ImeIndicator build script (ASCII only - compatible with PS 5.1 / GBK console)
# Detects MSVC (cl) or MinGW-w64 (g++), builds ImeIndicator.exe
# MinGW path: incremental (recompile only changed .cpp) + parallel compile
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
# NOTE: do NOT use $ErrorActionPreference='Stop' here - it makes PS 5.1
# abort on the first stderr line from g++, hiding the real compile errors.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$out = Join-Path $root 'ImeIndicator.exe'
$buildDir = Join-Path $root 'build'
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
$log = Join-Path $buildDir 'build.log'
$srcList = @('src\main.cpp','src\config.cpp','src\ime_monitor.cpp','src\floating.cpp','src\effect.cpp','src\fx.cpp','src\sand.cpp','src\settings.cpp','src\log.cpp')
$src = @()
foreach ($s in $srcList) { $src += Join-Path $root $s }

Remove-Item $log -ErrorAction SilentlyContinue
function Log([string]$line) { Add-Content -Path $log -Value $line; Write-Host $line }

# ---------- compiler detection ----------
$vcvars = $null
$gxx = $null
$msvc = $false

if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    $msvc = $true
} else {
    $roots = @(
        'C:\Program Files\Microsoft Visual Studio\2022',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022',
        'C:\Program Files\Microsoft Visual Studio\2019',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019'
    )
    foreach ($r in $roots) {
        if (Test-Path $r) {
            # edition 子目录（Community/Enterprise/BuildTools/...）—— GitHub runner 上路径含该层
            $f = Get-ChildItem -Path (Join-Path $r '*\VC\Auxiliary\Build\vcvars64.bat') -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($f) { $vcvars = $f.FullName; $msvc = $true; break }
        }
    }
}

if (-not $msvc) {
    $gxx = (Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    if (-not $gxx) {
        foreach ($p in @('C:\msys64\mingw64\bin\g++.exe','C:\mingw64\bin\g++.exe')) {
            if (Test-Path $p) { $gxx = $p; break }
        }
    }
}

if (-not $msvc -and -not $gxx) {
    Log 'NO_COMPILER: MSVC or MinGW-w64 g++ not found.'
    Log 'Install one of:'
    Log '  1) winget install -e --id Win32.G++        (MinGW-w64)'
    Log '  2) winget install -e --id Microsoft.VisualStudio.2022.BuildTools'
    exit 1
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

# NOTE: MinGW-w64 auto-embeds a default manifest (Common-Controls 6.0 + DPI).
# Do NOT embed a second manifest via windres - ld fails with
# "multiple non-default manifests" and visual styles may break.

if ($msvc) {
    # MSVC: full rebuild (fallback path)
    Remove-Item $out -ErrorAction SilentlyContinue
    $flags = '/nologo /W3 /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS'
    $libs  = '/link /SUBSYSTEM:WINDOWS gdiplus.lib imm32.lib shell32.lib comctl32.lib comdlg32.lib advapi32.lib user32.lib gdi32.lib ole32.lib oleaut32.lib'

    # embed visual-styles manifest: app.rc -> app.res (rc.exe ships with Windows SDK,
    # on PATH inside the vcvars environment). Without it ComCtl32 v6 is not requested.
    $resArg = ''
    $rcSrc = Join-Path $root 'res\app.rc'
    $resFile = Join-Path $buildDir 'app.res'
    if (Test-Path $rcSrc) {
        Remove-Item $resFile -ErrorAction SilentlyContinue
        $rcCmd = "rc /nologo /fo `"$resFile`" `"$rcSrc`""
        if ($vcvars) { $rcCmd = "`"$vcvars`" && $rcCmd" }
        elseif (-not (Get-Command rc.exe -ErrorAction SilentlyContinue)) { $rcCmd = $null }
        if ($rcCmd) {
            $resLines = cmd /c $rcCmd 2>&1
            $resLines | ForEach-Object { Add-Content -Path $log -Value $_; Write-Host $_ }
        }
        if (Test-Path $resFile) { $resArg = "`"$resFile`"" }
        else { Log 'WARN: rc.exe unavailable - manifest NOT embedded (no visual styles in MSVC build)' }
    }

    if ($vcvars) {
        $cmd = "`"$vcvars`" && cl $flags /Fe:`"$out`" " + ($src -join ' ') + " $resArg $libs"
        $outLines = cmd /c $cmd 2>&1
    } else {
        $outLines = cl $flags /Fe:"$out" $src $resArg $libs 2>&1
    }
    $outLines | ForEach-Object { Add-Content -Path $log -Value $_; Write-Host $_ }
} else {
    # ---------- MinGW: incremental + parallel ----------
    $objDir = Join-Path $buildDir 'obj'
    if (-not (Test-Path $objDir)) { New-Item -ItemType Directory -Path $objDir | Out-Null }

    # newest header mtime: any .h / version.rc newer than an .o forces recompile
    $headers = @(Get-ChildItem (Join-Path $root 'src') -Filter '*.h' -ErrorAction SilentlyContinue)
    if (Test-Path (Join-Path $root 'res\version.rc')) { $headers += Get-Item (Join-Path $root 'res\version.rc') }
    $hdrNewest = [datetime]::MinValue
    if ($headers) {
        $m = $headers | Measure-Object -Property LastWriteTime -Maximum
        if ($m -and $m.Maximum) { $hdrNewest = $m.Maximum }
    }

    $toCompile = @()
    $objs = @()
    foreach ($f in $src) {
        $obj = Join-Path $objDir ([IO.Path]::GetFileNameWithoutExtension($f) + '.o')
        $objs += $obj
        $objTime = [datetime]::MinValue
        if (Test-Path $obj) { $objTime = (Get-Item $obj).LastWriteTime }
        $srcTime = (Get-Item $f).LastWriteTime
        if (($objTime -ge $srcTime) -and ($hdrNewest -le $objTime)) {
            Log ("SKIP " + (Split-Path $f -Leaf) + " (up to date)")
        } else {
            $toCompile += ,@($f, $obj)
        }
    }

    # all up to date and exe exists: done
    if (($toCompile.Count -eq 0) -and (Test-Path $out)) {
        $sw.Stop()
        Log ("BUILD_RESULT: OK (up to date)   time " + $sw.Elapsed.TotalSeconds.ToString('0.0') + "s")
        Log "OUTPUT: $out"
        exit 0
    }

    # something changed: delete old exe to avoid false OK
    Remove-Item $out -ErrorAction SilentlyContinue

    # parallel compile each changed .cpp
    $procs = @()
    foreach ($pair in $toCompile) {
        $cf = $pair[0]
        $co = $pair[1]
        $errF = $co + '.err'
        Remove-Item $errF -ErrorAction SilentlyContinue
        $gccArgs = @('-O2','-std=c++17','-municode','-mwindows','-Wall','-finput-charset=UTF-8','-fexec-charset=UTF-8','-c',$cf,'-o',$co)
        Log ("CC " + (Split-Path $cf -Leaf))
        $p = Start-Process -FilePath $gxx -ArgumentList $gccArgs -NoNewWindow -RedirectStandardError $errF -PassThru
        $procs += $p
    }
    if ($procs.Count -gt 0) { $procs | Wait-Process }

    # collect stderr (errors/warnings) from each compile
    foreach ($pair in $toCompile) {
        $errF = $pair[1] + '.err'
        if (Test-Path $errF) {
            Get-Content $errF | ForEach-Object { Add-Content -Path $log -Value $_; Write-Host $_ }
            Remove-Item $errF -ErrorAction SilentlyContinue
        }
    }

    # ---------- link ----------
    # version resource: compile res\version.rc with windres (version info only,
    # no manifest -> no "multiple non-default manifests" conflict)
    $windres = Join-Path (Split-Path $gxx) 'windres.exe'
    if (Test-Path $windres) {
        $verObj = Join-Path $objDir 'version.o'
        Log 'RC version.rc'
        & $windres (Join-Path $root 'res\version.rc') $verObj 2>&1 | ForEach-Object { Add-Content -Path $log -Value $_; Write-Host $_ }
        if (Test-Path $verObj) { $objs += $verObj }
    } else {
        Log 'WARN: windres.exe not found next to g++ - exe will have no version info'
    }

    # note: gdi32/user32 needed explicitly when linking .o files (GDI functions: CreateDIBSection/SelectObject/CreateFontW/SetTextColor ...)
    # note: -mwindows required at LINK time - without it the exe is a console-subsystem app (black console appears)
    # -static: fully static link (libgcc/libstdc++/libwinpthread) so the exe runs on machines without MinGW runtime DLLs
    $linkLines = & $gxx -municode -mwindows $objs -o $out -static -lgdi32 -luser32 -lgdiplus -limm32 -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lole32 -loleaut32 -luuid 2>&1 | ForEach-Object { "$_" }
    $linkLines | ForEach-Object { Add-Content -Path $log -Value $_; Write-Host $_ }
}

$sw.Stop()
$ok = Test-Path $out
Log ("BUILD_RESULT: " + $(if ($ok) { 'OK' } else { 'FAILED' }) + "   time " + $sw.Elapsed.TotalSeconds.ToString('0.0') + "s")
if ($ok) { Log "OUTPUT: $out" }
exit $(if ($ok) { 0 } else { 1 })
