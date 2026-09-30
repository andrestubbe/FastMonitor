@echo off
chcp 65001 >nul
cd /d "%~dp0"

:: Driver installation, device restart, and removal require administrator rights.
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FastMonitor] Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process cmd -ArgumentList '/c \"%~f0\"' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

:: This script deliberately does not compile C++ or invoke Maven.
if not exist "src\main\resources\native\fastmonitor.dll" (
    echo [ERROR] The native DLL is missing. Run run-demo.bat once to build the demo.
    pause
    exit /b 1
)
if not exist "target\classes\fastmonitor\FastMonitor.class" (
    echo [ERROR] FastMonitor classes are missing. Run run-demo.bat once to build the demo.
    pause
    exit /b 1
)
if not exist "examples\Demo\target\classes\fastmonitor\Demo.class" (
    echo [ERROR] Demo classes are missing. Run run-demo.bat once to build the demo.
    pause
    exit /b 1
)
if not exist "examples\Demo\cp.txt" (
    echo [ERROR] The demo classpath is missing. Run run-demo.bat once to build the demo.
    pause
    exit /b 1
)

set "CP="
set /p CP=<"examples\Demo\cp.txt"
if not defined CP (
    echo [ERROR] The demo classpath is empty. Run run-demo.bat once to build the demo.
    pause
    exit /b 1
)

echo [FastMonitor] Starting the already-built Visual Showcase Demo...
java --enable-native-access=ALL-UNNAMED "-Djava.library.path=%~dp0src\main\resources\native;%~dp0release;%~dp0native" -cp "%~dp0examples\Demo\target\classes;%~dp0target\classes;%CP%" fastmonitor.Demo
set "RC=%ERRORLEVEL%"
if "%RC%"=="1" pause
exit /b %RC%
