# Changelog — FastMonitor 📜

All notable changes to **FastMonitor** will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.0] — 2026-09-27

### Added
- **Core Architecture**:
  - Direct JNI bindings (`FastMonitorNative`) communicating with Windows Indirect Display Driver (IDD) and Parsec VDD endpoints.
  - Safe, idiomatic Java 17 API wrapper (`FastMonitor`) with `AutoCloseable` lifecycle support.
  - Dynamic monitor lifecycle: `create`, `reconfigure`, `activate`, `deactivate`, and `destroy`.
  - JSON topology reporting via `FastMonitor.dumpAllMonitorsJson()`.
- **Native Subsystem**:
  - `native/fastmonitor.cpp` and `native/fastmonitor.h` implementing asynchronous `DeviceIoControl` with overlapped I/O.
  - Automatic `SetupDiGetClassDevs` and `SetupDiEnumDeviceInterfaces` discovery for virtual display adapter interfaces.
  - Background keepalive watchdog thread executing periodic heartbeats (`VDD_IOCTL_UPDATE`).
  - Seamless emulation fallback mode when hardware driver is not yet installed.
- **Tooling & Ecosystem**:
  - FastCore unified library loading integration.
  - `compile.bat` with automated Visual Studio (VS 2026/2022) and `JAVA_HOME` discovery.
  - `run-demo.bat` and `run-benchmark.bat` automated launcher scripts.
  - Complete documentation suite (`PHILOSOPHY.md`, `COMPILE.md`, `REFERENCE.md`, `ROADMAP.md`).
