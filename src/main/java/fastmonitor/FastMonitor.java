package fastmonitor;

import java.util.Objects;

/**
 * High-performance virtual monitor and display topology controller for Java.
 *
 * <p>FastMonitor enables dynamic, deterministic creation, reconfiguration,
 * and destruction of native virtual displays in Windows 10/11 using
 * Indirect Display Driver (IDD) technology.</p>
 *
 * <p>Key features:
 * <ul>
 *   <li>Create hardware-accelerated virtual displays on-the-fly (60–500 Hz, up to 8K)</li>
 *   <li>Fully integrated with Windows Desktop Window Manager (DWM) and DXGI outputs</li>
 *   <li>Zero-copy synergy with FastScreen, FastGPU, FastOverlay, and FastRobot</li>
 *   <li>Native C++ JNI bridge via DeviceIoControl and Parsec VDD architecture</li>
 *   <li>Safe lifecycle management via {@link AutoCloseable}</li>
 * </ul></p>
 *
 * @author Andre Stubbe
 * @version 0.1.0
 * @since 2026-09-27
 */
public final class FastMonitor implements AutoCloseable {

    /**
     * Configuration specification for a virtual monitor.
     */
    public static final class Config {
        public final int width;
        public final int height;
        public final int refreshHz;
        public final String name;

        public Config(int width, int height, int refreshHz, String name) {
            if (width <= 0) throw new IllegalArgumentException("Width must be > 0: " + width);
            if (height <= 0) throw new IllegalArgumentException("Height must be > 0: " + height);
            if (refreshHz <= 0) throw new IllegalArgumentException("Refresh rate must be > 0: " + refreshHz);
            this.width = width;
            this.height = height;
            this.refreshHz = refreshHz;
            this.name = Objects.requireNonNull(name, "name");
        }

        @Override
        public String toString() {
            return "Config{width=" + width + ", height=" + height + ", refreshHz=" + refreshHz + ", name='" + name + "'}";
        }
    }

    private final int id;
    private Config config;
    private boolean closed = false;

    private FastMonitor(int id, Config config) {
        this.id = id;
        this.config = config;
    }

    /**
     * Initializes the native FastMonitor backend.
     *
     * @return true if initialized successfully
     */
    public static synchronized boolean init() {
        return FastMonitorNative.initBackend();
    }

    /**
     * Shuts down the native FastMonitor backend.
     */
    public static synchronized void shutdown() {
        FastMonitorNative.shutdownBackend();
    }

    /**
     * Creates and attaches a new virtual monitor to Windows.
     *
     * @param config the monitor resolution, refresh rate, and identifier
     * @return a new FastMonitor instance
     * @throws IllegalStateException if the driver call fails
     */
    public static FastMonitor create(Config config) {
        Objects.requireNonNull(config, "config");
        init();
        int id = FastMonitorNative.createVirtualMonitor(
                config.width,
                config.height,
                config.refreshHz,
                config.name
        );
        if (id <= 0) {
            throw new IllegalStateException("Failed to create virtual monitor: " + config);
        }
        return new FastMonitor(id, config);
    }

    /**
     * Returns the logical ID of this virtual monitor.
     */
    public int id() {
        return id;
    }

    /**
     * Returns the current configuration of this monitor.
     */
    public Config config() {
        return config;
    }

    /**
     * Reconfigures resolution and refresh rate of this virtual monitor.
     *
     * @param newConfig new configuration parameters
     * @return true if successfully reconfigured
     */
    public synchronized boolean reconfigure(Config newConfig) {
        checkNotClosed();
        Objects.requireNonNull(newConfig, "newConfig");
        boolean ok = FastMonitorNative.configureVirtualMonitor(
                id,
                newConfig.width,
                newConfig.height,
                newConfig.refreshHz
        );
        if (ok) {
            this.config = newConfig;
        }
        return ok;
    }

    /**
     * Activates this virtual monitor in the OS topology.
     */
    public synchronized boolean activate() {
        checkNotClosed();
        return FastMonitorNative.activateVirtualMonitor(id);
    }

    /**
     * Deactivates this virtual monitor from the OS topology without destroying it.
     */
    public synchronized boolean deactivate() {
        checkNotClosed();
        return FastMonitorNative.deactivateVirtualMonitor(id);
    }

    /**
     * Destroys this virtual monitor and detaches it from Windows.
     */
    public synchronized boolean destroy() {
        if (closed) {
            return false;
        }
        closed = true;
        return FastMonitorNative.destroyVirtualMonitor(id);
    }

    @Override
    public void close() {
        destroy();
    }

    /**
     * Returns a JSON representation of all currently active virtual monitors.
     */
    public static String dumpAllMonitorsJson() {
        return FastMonitorNative.listVirtualMonitors();
    }

    private void checkNotClosed() {
        if (closed) {
            throw new IllegalStateException("Virtual monitor #" + id + " has already been destroyed.");
        }
    }
}
