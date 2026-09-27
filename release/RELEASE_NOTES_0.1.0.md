## FastMonitor 0.1.0

### What's New

- **Native Windows IDD Display Creation** — First release of FastMonitor, enabling dynamic creation and destruction of hardware-accelerated virtual displays directly from Java 17+.
- **Parsec VDD User-Mode Driver Integration** — Directly interfaces with Parsec VDD via Win32 `DeviceIoControl` using overlapped I/O and automated device discovery.
- **Zero GC Overhead** — Primitive handle management and off-heap state structures ensure zero JVM garbage collection pauses.
- **Background Keepalive Watchdog** — Automated background heartbeat thread prevents Windows driver watchdog disconnects.
- **Dynamic Reconfiguration** — Change resolution (up to 8K) and refresh rate (60–500 Hz) on running virtual monitors on-the-fly.
- **AutoCloseable Lifecycle** — Idiomatic Java resource management with guaranteed cleanup of display adapters on shutdown.

### Related Projects

- [FastScreen](https://github.com/andrestubbe/FastScreen) — Ultra-fast 2000 FPS DXGI Desktop Duplication
- [FastGPU](https://github.com/andrestubbe/FastGPU) — DirectX 12 hardware acceleration for Java
- [FastCore](https://github.com/andrestubbe/FastCore) — Native DLL extraction and OS abstraction layer
