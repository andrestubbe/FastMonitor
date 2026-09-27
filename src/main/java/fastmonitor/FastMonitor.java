package fastmonitor;

import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * High-performance virtual monitor and display topology controller for Java.
 *
 * <p>FastMonitor enables dynamic, deterministic creation, reconfiguration,
 * and destruction of native virtual displays in Windows 10/11 using
 * Indirect Display Driver (IDD) technology.</p>
 *
 * @author Andre Stubbe
 * @version 0.1.0
 * @since 2026-09-27
 */
public final class FastMonitor implements AutoCloseable {

    private static final AtomicBoolean INITIALIZED = new AtomicBoolean(false);

    /**
     * Configuration specification for a virtual monitor.
     */
    public static final class Config {
        public final int width;
        public final int height;
        public final int refreshHz;
        public final String name;

        public Config(int width, int height, int refreshHz, String name) {
            if (width < 640 || width > 7680) {
                throw new IllegalArgumentException("Width out of supported bounds [640..7680]: " + width);
            }
            if (height < 480 || height > 4320) {
                throw new IllegalArgumentException("Height out of supported bounds [480..4320]: " + height);
            }
            if (refreshHz < 24 || refreshHz > 500) {
                throw new IllegalArgumentException("Refresh rate out of supported bounds [24..500]: " + refreshHz);
            }
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
    private volatile Config config;
    private final AtomicBoolean closed = new AtomicBoolean(false);

    private FastMonitor(int id, Config config) {
        this.id = id;
        this.config = config;
    }

    /**
     * Initializes the native FastMonitor backend.
     *
     * @return true if initialized successfully
     */
    public static boolean init() {
        if (INITIALIZED.get()) {
            return true;
        }
        synchronized (FastMonitor.class) {
            if (INITIALIZED.get()) {
                return true;
            }
            boolean ok = FastMonitorNative.initBackend();
            if (ok) {
                INITIALIZED.set(true);
            }
            return ok;
        }
    }

    /**
     * Shuts down the native FastMonitor backend.
     */
    public static synchronized void shutdown() {
        if (!INITIALIZED.get()) {
            return;
        }
        FastMonitorNative.shutdownBackend();
        INITIALIZED.set(false);
    }

    /**
     * Checks if the physical Parsec VDD driver is present on this system.
     */
    public static boolean isDriverPresent() {
        init();
        return FastMonitorNative.isDriverPresent();
    }

    /**
     * Returns the driver version, or 0 if running in emulation mode.
     */
    public static int driverVersion() {
        init();
        return FastMonitorNative.driverVersion();
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
        if (!INITIALIZED.get()) {
            init();
        }
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
     * Reconfigures resolution and refresh rate using primitive values (zero-allocation hot-path).
     */
    public boolean reconfigure(int width, int height, int refreshHz) {
        checkNotClosed();
        boolean ok = FastMonitorNative.configureVirtualMonitor(id, width, height, refreshHz);
        if (ok) {
            this.config = new Config(width, height, refreshHz, this.config.name);
        }
        return ok;
    }

    /**
     * Reconfigures resolution and refresh rate of this virtual monitor.
     */
    public boolean reconfigure(Config newConfig) {
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
    public boolean activate() {
        checkNotClosed();
        return FastMonitorNative.activateVirtualMonitor(id);
    }

    /**
     * Deactivates this virtual monitor from the OS topology without destroying it.
     */
    public boolean deactivate() {
        checkNotClosed();
        return FastMonitorNative.deactivateVirtualMonitor(id);
    }

    /**
     * Destroys this virtual monitor and detaches it from Windows.
     */
    public boolean destroy() {
        if (closed.get()) {
            return false;
        }
        boolean ok = FastMonitorNative.destroyVirtualMonitor(id);
        if (ok) {
            closed.set(true);
        }
        return ok;
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
        if (closed.get()) {
            throw new IllegalStateException("Virtual monitor #" + id + " has already been destroyed.");
        }
    }
}
