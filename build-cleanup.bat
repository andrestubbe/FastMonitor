@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

echo [FastMonitor] Building VDD Cleanup Tool...

:: Find VS using vswhere.exe
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)

if not defined VS_PATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
    )
)

if defined VS_PATH (
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    echo [ERROR] Visual Studio C++ compiler not found!
    pause
    exit /b 1
)

cd /d "%~dp0native"

cl /EHsc /O2 /W3 /nologo ^
   vdd_remove.cpp ^
   /link ^
   setupapi.lib ^
   /OUT:vdd_remove.exe ^
   /MACHINE:X64

if errorlevel 1 (
    echo [ERROR] Compilation failed!
    cd /d "%~dp0"
    pause
    exit /b 1
)

cd /d "%~dp0"
echo.
echo [SUCCESS] vdd_remove.exe built at native\vdd_remove.exe
echo [INFO] Run as Administrator to remove active VDD monitors.
echo.
pause
