package fastmonitor;

import fastcore.FastCore;

/**
 * Low-level JNI bridge for FastMonitor.
 *
 * <p>Communicates directly with the native C++ user-mode display driver
 * subsystem and Parsec VDD kernel/UMDF interface via DeviceIoControl.</p>
 *
 * @author Andre Stubbe
 * @version 0.1.0
 * @since 2026-09-27
 */
public final class FastMonitorNative {

    static {
        try {
            FastCore.loadLibrary("fastmonitor");
        } catch (Throwable t) {
            String libName = System.getProperty("fastmonitor.native", "fastmonitor");
            System.loadLibrary(libName);
        }
    }

    private FastMonitorNative() {
    }

    public static native boolean initBackend();
    public static native void shutdownBackend();

    public static native int createVirtualMonitor(
            int width,
            int height,
            int refreshHz,
            String name
    );

    public static native boolean destroyVirtualMonitor(int logicalId);

    public static native boolean configureVirtualMonitor(
            int logicalId,
            int width,
            int height,
            int refreshHz
    );

    public static native String listVirtualMonitors();

    public static native boolean activateVirtualMonitor(int logicalId);

    public static native boolean deactivateVirtualMonitor(int logicalId);
}
