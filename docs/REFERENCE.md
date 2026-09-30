# FastMonitor Reference 📘

Detailed technical specification, memory model, and JNI contracts for **FastMonitor**.

---

## 1. Hardware & Driver Model

FastMonitor targets the signed MikeTheTech Virtual Display Driver (MttVDD), a Windows Indirect Display Driver built on IddCx:

*   The driver's root-enumerated device instance is `ROOT\DISPLAY\0000` with hardware ID `Root\MttVDD`.
*   `installDriver()` downloads the pinned MikeTheTech VDD and NefCon release archives and checks their SHA-256 hashes before staging them for installation.
*   VDD monitor slots are configured through `C:\VirtualDisplayDriver\vdd_settings.xml`; FastMonitor changes the monitor count and restarts the PnP device to apply it.
*   FastMonitor stages per-display resolution and refresh-rate settings, then commits them through the Windows display API. Create/configure report failure if Windows rejects or cannot apply the requested mode.
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

1. **AutoCloseable**: In hardware mode, destroy or close monitors in reverse creation order. MttVDD settings store only a monitor count, so FastMonitor refuses to remove a non-highest slot rather than risk removing a different physical display. `destroy()` returns `false` for an out-of-order request; `close()` reports it with `IllegalStateException`. Destroying the highest slot decrements the count and restarts the device.
2. **Display modes**: `create()` adds the MttVDD slot and applies its requested mode before adding it to the Java-visible monitor state. If lookup or mode application fails, FastMonitor restores the previous monitor count and restarts the driver to remove the provisional slot. `reconfigure()` updates Java state only after Windows accepts the new mode; on failure it attempts to restore the previous Windows mode and returns `false`.
3. **Backend Shutdown**: `shutdown()` sets any remaining VDD monitor count to zero and restarts the device as a final cleanup attempt.
4. **Demo Device Cleanup**: After shutdown, the demo calls `removeDriverDevice()`. This removes the MttVDD device node while leaving its driver package in Windows Driver Store; the next demo run downloads and verifies the pinned packages, then recreates the node.
5. **Graceful Fallback**: If the MttVDD device is absent, FastMonitor operates in software emulation mode without creating a Windows display.
