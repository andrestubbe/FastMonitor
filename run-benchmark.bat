@echo off
setlocal
chcp 65001 > nul
cd /d "%~dp0"
set "MAVEN_OPTS=--enable-native-access=ALL-UNNAMED -Dorg.slf4j.simpleLogger.defaultLogLevel=warn"

echo ===================================================
echo  Building FastMonitor Benchmark
echo ===================================================

call mvn -q clean install -DskipTests 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] FastMonitor install failed!
    pause
    exit /b %ERRORLEVEL%
)

echo [FastMonitor] Native benchmark run completed.
pause
