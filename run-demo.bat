@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo [FastMonitor] Compiling Native C++ library...
call compile.bat >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Native C++ compilation failed.
    call compile.bat
    pause
    exit /b %ERRORLEVEL%
)

echo [FastMonitor] Building library...
call mvn clean install -DskipTests -q
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] FastMonitor build failed.
    pause
    exit /b %ERRORLEVEL%
)

powershell -NoProfile -Command "Unblock-File -Path '%USERPROFILE%\.fastcore\native\fastmonitor\*', '%~dp0src\main\resources\native\*', '%~dp0release\*' -ErrorAction SilentlyContinue" >nul 2>&1

echo [FastMonitor] Compiling Visual Demo...
cd examples\Demo
call mvn compile "-Dmdep.outputFile=cp.txt" dependency:build-classpath -DincludeScope=runtime -q
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Demo compilation failed.
    cd ..\..
    pause
    exit /b %ERRORLEVEL%
)

echo [FastMonitor] Starting Visual Showcase Demo...
set /p CP=<cp.txt
java --enable-native-access=ALL-UNNAMED "-Djava.library.path=%~dp0src\main\resources\native;%~dp0release;%~dp0native" -cp "target\classes;%CP%" fastmonitor.Demo

cd ..\..
pause
