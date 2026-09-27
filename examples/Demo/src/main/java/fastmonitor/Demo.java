package fastmonitor;

import fastmonitor.FastMonitor.Config;

/**
 * Visual Showcase Demo for FastMonitor.
 *
 * <p>Demonstrates creating, reconfiguring, activating, and destroying
 * virtual display monitors directly from Java.</p>
 */
public class Demo {

    public static void main(String[] args) {
        System.out.println("=================================================");
        System.out.println("       ⚡ FastMonitor Visual Showcase Demo        ");
        System.out.println("=================================================");
        System.out.println();

        System.out.println("[INFO] Initializing FastMonitor backend...");
        boolean initialized = FastMonitor.init();
        System.out.println("[INFO] Backend initialized: " + initialized);

        Config initialConfig = new Config(1920, 1080, 60, "FastMonitor-Virtual-1");
        System.out.println("[INFO] Creating virtual monitor: " + initialConfig);

        try (FastMonitor monitor = FastMonitor.create(initialConfig)) {
            System.out.println("[SUCCESS] Virtual monitor created with ID: " + monitor.id());

            boolean activated = monitor.activate();
            System.out.println("[INFO] Monitor active in Windows topology: " + activated);

            System.out.println("[INFO] Current monitors dump:");
            System.out.println(FastMonitor.dumpAllMonitorsJson());

            System.out.println();
            System.out.println("[INFO] Reconfiguring monitor to 2560x1440 @ 144Hz...");
            Config gamingConfig = new Config(2560, 1440, 144, "FastMonitor-Virtual-1-Gaming");
            boolean reconfigured = monitor.reconfigure(gamingConfig);
            System.out.println("[INFO] Reconfiguration successful: " + reconfigured);
            System.out.println("[INFO] Updated config: " + monitor.config());

            System.out.println("[INFO] Updated monitors dump:");
            System.out.println(FastMonitor.dumpAllMonitorsJson());

            System.out.println();
            System.out.println("[INFO] Deactivating monitor...");
            monitor.deactivate();

            System.out.println("[INFO] Closing monitor resources...");
        } catch (Exception e) {
            System.err.println("[ERROR] Failed to manage virtual monitor: " + e.getMessage());
            e.printStackTrace();
        } finally {
            FastMonitor.shutdown();
            System.out.println("[INFO] FastMonitor backend shut down cleanly.");
        }

        System.out.println();
        System.out.println("=================================================");
        System.out.println("               Demo completed!                   ");
        System.out.println("=================================================");
    }
}
