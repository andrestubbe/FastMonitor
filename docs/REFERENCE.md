# FastMonitor Reference 📘

Detailed technical specification, memory model, and JNI contracts for **FastMonitor**.

---

## 1. Hardware & Driver Model

FastMonitor targets the signed MikeTheTech Virtual Display Driver (MttVDD), a Windows Indirect Display Driver built on IddCx:

*   The driver's root-enumerated device instance is `ROOT\DISPLAY\0000` with hardware ID `Root\MttVDD`.
*   `installDriver()` downloads the pinned MikeTheTech VDD and NefCon release archives and checks their SHA-256 hashes before staging them for installation.
*   VDD monitor slots are configured through `C:\VirtualDisplayDriver\vdd_settings.xml`; FastMonitor changes the monitor count and restarts the PnP device to apply it.
*   FastMonitor uses Windows display APIs to apply resolution and refresh-rate modes to the resulting display outputs.
*   The VDD package supports the modes listed by its configuration and Windows display stack; FastMonitor validates requested values against its Java API bounds.

---

## 2. Architecture Decision: JNI vs. FFM

FastMonitor implements a hand-tuned C++ JNI bridge rather than Java 22+ Foreign Function & Memory (FFM):
1. **LTS Compatibility**: Guarantees compatibility with Java 17 LTS across the FastJava ecosystem without requiring preview flags or Java 22+.
2. **Encapsulation of Windows PnP Operations**: `SetupAPI` and Configuration Manager calls, MttVDD configuration updates, display-mode changes, and device-node removal are implemented in the native C++ bridge.
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
    public static boolean installDriver();
    public static boolean removeDriverDevice();
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

1. **AutoCloseable**: Instances can be managed within `try-with-resources` blocks. Upon exit, `destroy()` removes the monitor slot from the VDD settings and restarts the device.
2. **Backend Shutdown**: `shutdown()` sets any remaining VDD monitor count to zero and restarts the device as a final cleanup attempt.
3. **Demo Device Cleanup**: After shutdown, the demo calls `removeDriverDevice()`. This removes the MttVDD device node while leaving its driver package in Windows Driver Store; the next demo run downloads and verifies the pinned packages, then recreates the node.
4. **Graceful Fallback**: If the MttVDD device is absent, FastMonitor operates in software emulation mode without creating a Windows display.
