package fastmonitor;

import fastcore.FastCore;

/**
 * Low-level JNI bridge for FastMonitor.
 *
 * <p>Communicates directly with the native C++ backend which controls the
 * MikeTheTech Virtual Display Driver (MttVDD) via XML configuration,
 * Windows PnP device management, and device-node removal.</p>
 *
 * @author Andre Stubbe
 * @version 0.2.0
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
    public static native void    shutdownBackend();

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

    public static native String  listVirtualMonitorsJson();

    public static native boolean activateVirtualMonitor(int logicalId);

    public static native boolean deactivateVirtualMonitor(int logicalId);

    public static native boolean isDriverPresent();

    /** Removes only the MttVDD device node, not the installed driver package. */
    public static native boolean removeDriverDevice();

    public static native boolean isEmulationMode();

    public static native int     driverVersion();

    /**
     * Starts the staged driver-only install command with normal Windows elevation.
     * Must be called before {@link #initBackend()}.
     */
    public static native boolean installDriver(String commandPath, String workingDirectory);
}
