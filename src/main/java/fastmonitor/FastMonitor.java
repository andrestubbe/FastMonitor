package fastmonitor;

import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

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

    private static final int STATE_OPEN = 0;
    private static final int STATE_DESTROYING = 1;
    private static final int STATE_CLOSED = 2;

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
    private final AtomicInteger state = new AtomicInteger(STATE_OPEN);

    private static final java.util.concurrent.atomic.AtomicLong BACKEND_GENERATION = new java.util.concurrent.atomic.AtomicLong(1);

    private final long generation;

    private FastMonitor(int id, Config config, long generation) {
        this.id = id;
        this.config = config;
        this.generation = generation;
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
        BACKEND_GENERATION.incrementAndGet();
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
     * @throws IllegalStateException if backend initialization or driver creation fails
     */
    public static FastMonitor create(Config config) {
        Objects.requireNonNull(config, "config");
        if (!INITIALIZED.get() && !init()) {
            throw new IllegalStateException("FastMonitor backend initialization failed");
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
        return new FastMonitor(id, config, BACKEND_GENERATION.get());
    }

    /**
     * Returns the logical ID of this virtual monitor.
     */
    public int id() {
        return id;
    }

    /**
     * Returns the current configuration snapshot of this monitor.
     */
    public Config config() {
        return config;
    }

    public static void validateMode(int width, int height, int refreshHz) {
        if (width < 640 || width > 7680) {
            throw new IllegalArgumentException("Width out of supported bounds [640..7680]: " + width);
        }
        if (height < 480 || height > 4320) {
            throw new IllegalArgumentException("Height out of supported bounds [480..4320]: " + height);
        }
        if (refreshHz < 24 || refreshHz > 500) {
            throw new IllegalArgumentException("Refresh rate out of supported bounds [24..500]: " + refreshHz);
        }
    }

    /**
     * Reconfigures resolution and refresh rate using primitive values.
     * Skips allocation if dimensions and refresh rate are identical.
     */
    public boolean reconfigure(int width, int height, int refreshHz) {
        checkNotClosed();
        checkBackendActive();
        validateMode(width, height, refreshHz);
        Config cur = this.config;
        if (cur.width == width && cur.height == height && cur.refreshHz == refreshHz) {
            return true;
        }
        boolean ok = FastMonitorNative.configureVirtualMonitor(id, width, height, refreshHz);
        if (ok) {
            this.config = new Config(width, height, refreshHz, cur.name);
        }
        return ok;
    }

    /**
     * Reconfigures resolution and refresh rate of this virtual monitor.
     */
    public boolean reconfigure(Config newConfig) {
        checkNotClosed();
        checkBackendActive();
        Objects.requireNonNull(newConfig, "newConfig");
        Config cur = this.config;
        if (cur.width == newConfig.width && cur.height == newConfig.height && cur.refreshHz == newConfig.refreshHz && cur.name.equals(newConfig.name)) {
            return true;
        }
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
     * Updates the monitor active flag in the internal state model.
     */
    public boolean activate() {
        checkNotClosed();
        checkBackendActive();
        return FastMonitorNative.activateVirtualMonitor(id);
    }

    /**
     * Updates the monitor inactive flag in the internal state model.
     */
    public boolean deactivate() {
        checkNotClosed();
        checkBackendActive();
        return FastMonitorNative.deactivateVirtualMonitor(id);
    }

    /**
     * Destroys this virtual monitor and detaches it from Windows.
     */
    public boolean destroy() {
        if (!state.compareAndSet(STATE_OPEN, STATE_DESTROYING)) {
            return false;
        }
        try {
            boolean ok = FastMonitorNative.destroyVirtualMonitor(id);
            state.set(ok ? STATE_CLOSED : STATE_OPEN);
            return ok;
        } catch (Throwable t) {
            state.set(STATE_OPEN);
            throw t;
        }
    }

    @Override
    public void close() {
        destroy();
    }

    /**
     * Returns a JSON representation of all currently active virtual monitors.
     */
    public static String dumpAllMonitorsJson() {
        if (!INITIALIZED.get()) {
            return "[]";
        }
        return FastMonitorNative.listVirtualMonitors();
    }

    private void checkNotClosed() {
        if (state.get() != STATE_OPEN) {
            throw new IllegalStateException("Virtual monitor #" + id + " is not open.");
        }
    }

    private void checkBackendActive() {
        if (!INITIALIZED.get() || generation != BACKEND_GENERATION.get()) {
            throw new IllegalStateException("FastMonitor backend has been shut down or restarted.");
        }
    }
}
