# FastMonitor Reference 📘

Detailed technical specification, memory model, and JNI contracts for **FastMonitor**.

---

## 1. Hardware & Driver Model

FastMonitor utilizes an Indirect Display Driver (IDD / IddCx) bridge model targeting the Windows display subsystem:

*   **Driver Architecture — Windows Indirect Display Driver (IDD)**:
    *   Targets Windows 10/11 User Mode Driver Framework (UMDF).
    *   Communicates with the driver's device interface GUID `{00b41627-04c4-429e-a26e-0265cf50c8fa}`.
    *   Exposes virtual monitors directly to DXGI and the Windows Desktop Window Manager (DWM).
    *   Supports arbitrary resolutions from 640x480 to 7680x4320 (8K) and refresh rates up to 500 Hz.
*   **IOCTL Protocol**:
    *   `VDD_IOCTL_ADD (0x0022e004)`: Requests creation and attachment of a virtual monitor.
    *   `VDD_IOCTL_REMOVE (0x0022a008)`: Detaches and destroys a virtual display output.
    *   `VDD_IOCTL_UPDATE (0x0022a00c)`: Periodic keepalive ping to ensure the driver does not auto-disconnect idle monitors.
    *   `VDD_IOCTL_VERSION (0x0022e010)`: Queries driver capability and version numbers.

---

## 2. API Specification

### `FastMonitor` Class

```java
package fastmonitor;

public final class FastMonitor implements AutoCloseable {
    public static boolean init();
    public static void shutdown();
    public static FastMonitor create(Config config);
    public int id();
    public Config config();
    public boolean reconfigure(Config newConfig);
    public boolean activate();
    public boolean deactivate();
    public boolean destroy();
    public void close();
    public static String dumpAllMonitorsJson();
}
```

### `FastMonitor.Config`

Encapsulates the virtual monitor properties:
*   `int width`: Horizontal resolution in pixels (> 0).
*   `int height`: Vertical resolution in pixels (> 0).
*   `int refreshHz`: Refresh rate in Hertz (e.g. 60, 120, 144, 240).
*   `String name`: Human-readable identifier for diagnostics.

---

## 3. Error Handling and Lifecycle

1. **AutoCloseable**: Instances can be managed within `try-with-resources` blocks. Upon exit, `destroy()` is called automatically to free OS display outputs.
2. **Graceful Fallback**: If the Parsec VDD driver is absent from the host system, FastMonitor's native bridge operates in a graceful software tracking mode, enabling headless testing and mock validation without crashes.
