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
*   **Official IOCTL Protocol (Parsec VDD)**:
    *   `VDD_IOCTL_ADD (0x0022e004)`: Requests creation and attachment of a virtual monitor slot. Takes **no input payload**; returns 32-bit driver index.
    *   `VDD_IOCTL_REMOVE (0x0022a008)`: Detaches and destroys a virtual display output. Input is a 16-bit big-endian driver slot index (`UINT16`).
    *   `VDD_IOCTL_UPDATE (0x0022a00c)`: Periodic keepalive ping sent every **100 ms** to prevent driver watchdog disconnect.
    *   `VDD_IOCTL_VERSION (0x0022e010)`: Queries driver version and capability flags.
*   **Resolution and Refresh Mode Management**:
    *   Virtual display resolution and refresh rate are not configured via IOCTL payload.
    *   Modes are committed to the Windows display subsystem via Win32 Connecting and Configuring Displays (CCD) API (`ChangeDisplaySettingsExA`) with `DEVMODEA` parameters.

---

## 2. Architecture Decision: JNI vs. FFM

FastMonitor implements a hand-tuned C++ JNI bridge rather than Java 22+ Foreign Function & Memory (FFM):
1. **LTS Compatibility**: Guarantees compatibility with Java 17 LTS across the FastJava ecosystem without requiring preview flags or Java 22+.
2. **Encapsulation of Complex Win32 Driver Interfaces**: `SetupAPI` device enumeration with variable-length struct layouts (`SP_DEVICE_INTERFACE_DETAIL_DATA`), non-blocking overlapped Win32 I/O, device IOCTL marshalling, and a native watchdog thread are compiled directly in C++ with zero overhead.
3. **Future Panama Upgrade Path**: FastCore already encapsulates FFM symbol lookups; once FastJava transitions its baseline beyond Java 21, FastMonitor can expose optional zero-glue FFM downcall handles without altering its public API.

---

## 3. API Specification

### `FastMonitor` Class

```java
package fastmonitor;

public final class FastMonitor implements AutoCloseable {
    public static boolean init();
    public static void shutdown();
    public static boolean isDriverPresent();
    public static int driverVersion();
    public static FastMonitor create(Config config);
    public int id();
    public Config config();
    public boolean reconfigure(int width, int height, int refreshHz);
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
*   `int width`: Horizontal resolution in pixels [640..7680].
*   `int height`: Vertical resolution in pixels [480..4320].
*   `int refreshHz`: Refresh rate in Hertz [24..500].
*   `String name`: Human-readable identifier for diagnostics.

---

## 4. Error Handling and Lifecycle

1. **AutoCloseable**: Instances can be managed within `try-with-resources` blocks. Upon exit, `destroy()` is called automatically to free OS display outputs.
2. **Atomic Lifecycle**: `AtomicBoolean` ensures that failed destroys can be retried and that multiple concurrent close attempts do not race.
3. **Graceful Fallback**: If the Parsec VDD driver is absent from the host system, FastMonitor operates in a graceful software tracking mode, enabling headless testing and mock validation without crashes.
