@echo off
setlocal
REM Convenience wrapper: kills running instance, builds, then launches the new exe.
REM Use "build.bat -NoRun" to skip the auto-launch. For scripted/CI builds call build.ps1 directly.
echo ============================================
echo   ImeIndicator build script
echo ============================================

set RUNAPP=1
if /i "%~1"=="-NoRun" set RUNAPP=0

REM Kill running instance so the exe is not locked during compile
taskkill /IM ImeIndicator.exe /F >nul 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
set EXITCODE=%ERRORLEVEL%

REM Show build result and any errors/warnings from the log
if exist "%~dp0build\build.log" (
    findstr /C:"BUILD_RESULT" "%~dp0build\build.log" >nul 2>&1 && (
        echo.
        echo -------- build result --------
        findstr /C:"BUILD_RESULT" "%~dp0build\build.log"
    )
    findstr /I /C:"error" /C:"warning" /C:"merge failure" "%~dp0build\build.log" >nul 2>&1 && (
        echo.
        echo -------- errors / warnings --------
        findstr /I /C:"error" /C:"warning" /C:"merge failure" "%~dp0build\build.log"
    )
)

if exist "%~dp0ImeIndicator.exe" (
    echo.
    echo BUILD SUCCESS
    if "%RUNAPP%"=="1" (
        echo Launching... ^(use "build.bat -NoRun" to skip^)
        start "" "%~dp0ImeIndicator.exe"
    )
) else (
    echo.
    echo BUILD FAILED: see build.log for details.
)

echo.
pause
exit /b %EXITCODE%
