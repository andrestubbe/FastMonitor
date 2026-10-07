# Changelog — FastMonitor 📜

All notable changes to **FastMonitor** will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.0] — 2026-10-07

### Added
- **Core Architecture**:
  - Direct JNI bindings (`FastMonitorNative`) communicating with Windows Indirect Display Driver (IDD / IddCx) and MikeTheTech VDD endpoints.
  - Safe, idiomatic Java 17 API wrapper (`FastMonitor`) with `AutoCloseable` lifecycle support.
  - Dynamic monitor lifecycle: `create`, `reconfigure`, `activate`, `deactivate`, and `destroy`.
  - JSON topology reporting via `FastMonitor.dumpAllMonitorsJson()`.
- **Native Subsystem**:
  - `native/fastmonitor.cpp` and `native/fastmonitor.h` implementing asynchronous `DeviceIoControl` with overlapped I/O.
  - Automatic `SetupDiGetClassDevs` and `SetupDiEnumDeviceInterfaces` discovery for virtual display adapter interfaces.
  - Background keepalive watchdog thread executing periodic heartbeats (`VDD_IOCTL_UPDATE`).
  - Seamless emulation fallback mode when hardware driver is not yet installed.
- **Driver Integration & Automation**:
  - MikeTheTech VDD driver-only integration via verified NefCon utility and Windows Plug and Play (PnP) APIs.
  - Pinned VDD and signed NefCon archive downloads with strict SHA-256 verification and Local Machine Trusted Publisher certificate installation.
  - Device node teardown on demo shutdown while retaining the driver package in Windows Driver Store.
- **Tooling & Launchers**:
  - `run-demo.bat`: Full-pipeline runner (compiles C++ DLL with MSVC, builds Maven modules, compiles demo, and executes).
  - `run-demo-ready.bat`: Ultra-fast launcher for pre-built binaries that requests UAC elevation and starts the visual showcase instantly (< 100 ms) without invoking Maven or MSVC compilers.
  - `run-benchmark.bat`: Official JMH microbenchmark suite.
  - Complete documentation suite (`PHILOSOPHY.md`, `COMPILE.md`, `REFERENCE.md`, `ROADMAP.md`, `CHANGELOG.md`).
