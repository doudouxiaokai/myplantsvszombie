@echo off
setlocal
cd /d "%~dp0"

rem ------------------------------------------------------------
rem  Rebuild this solution with MSBuild, then start the NEW exe.
rem  Double-click this file if the game seems to run old code.
rem ------------------------------------------------------------

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"

if not defined MSBUILD (
    echo [ERROR] MSBuild.exe not found.
    pause
    exit /b 1
)

echo [1/2] Rebuilding x64 / Debug ...
"%MSBUILD%" "%~dp0myplantsvszombie.sln" /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m > "%~dp0build_log.txt" 2>&1
if errorlevel 1 (
    echo.
    type "%~dp0build_log.txt"
    echo.
    echo [ERROR] Build failed. The full log is build_log.txt
    pause
    exit /b 1
)

echo.
echo [2/2] Build OK. Starting the new exe ...
start "" "%~dp0x64\Debug\myplantsvszombie.exe"
