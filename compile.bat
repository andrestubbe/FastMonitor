@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

echo ===========================================
echo FastMonitor Native Compilation
echo ===========================================
echo.

:: Try to find VS using vswhere.exe
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
)

if not defined VS_PATH (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    )
)

if defined VS_PATH (
    echo [INFO] Found Visual Studio at: %VS_PATH%
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    where cl >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Visual Studio C++ compiler not found!
        pause
        exit /b 1
    )
)

:: Check JAVA_HOME
if not defined JAVA_HOME (
    if exist "C:\Program Files\java\jdk-21.0.12.1" (
        set "JAVA_HOME=C:\Program Files\java\jdk-21.0.12.1"
    ) else if exist "C:\Program Files\Java\jdk-21.0.12.1" (
        set "JAVA_HOME=C:\Program Files\Java\jdk-21.0.12.1"
    ) else if exist "C:\Program Files\Java\jdk-25" (
        set "JAVA_HOME=C:\Program Files\Java\jdk-25"
    ) else if exist "C:\Program Files\Eclipse Adoptium\jdk-17-hotspot" (
        set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17-hotspot"
    )
)

if not defined JAVA_HOME (
    echo [ERROR] JAVA_HOME not set!
    pause
    exit /b 1
)

echo [INFO] JAVA_HOME: %JAVA_HOME%
echo [INFO] Compiling native DLL...
echo.

:: Create native output directory if not exists
if not exist native mkdir native

:: Compile native DLL
cd /d "%~dp0native"

cl /LD /EHsc /O2 /W3 /nologo ^
   /I"%JAVA_HOME%\include" ^
   /I"%JAVA_HOME%\include\win32" ^
   fastmonitor.cpp ^
   /link ^
   user32.lib ^
   gdi32.lib ^
   setupapi.lib ^
   cfgmgr32.lib ^
   newdev.lib ^
   shell32.lib ^
   /OUT:fastmonitor.dll ^
   /MACHINE:X64

if errorlevel 1 (
    echo.
    echo [ERROR] Native compilation failed!
    cd /d "%~dp0"
    exit /b 1
)

cd /d "%~dp0"

echo.
echo [SUCCESS] Native DLL compiled successfully!
echo [INFO] Output: native\fastmonitor.dll
echo.

:: Deploy to resources and fastcore cache
if not exist "src\main\resources\native" mkdir "src\main\resources\native"
copy /Y "native\fastmonitor.dll" "src\main\resources\native\fastmonitor.dll" >nul

if not exist "%USERPROFILE%\.fastcore\native\fastmonitor" mkdir "%USERPROFILE%\.fastcore\native\fastmonitor"
copy /Y "native\fastmonitor.dll" "%USERPROFILE%\.fastcore\native\fastmonitor\fastmonitor.dll" >nul

echo [INFO] Deployed fastmonitor.dll to resources and ~/.fastcore/native/fastmonitor/
echo.
