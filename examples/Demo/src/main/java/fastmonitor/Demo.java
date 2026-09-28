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

        if (!FastMonitor.isDriverPresent()) {
            System.out.println("[INFO] No MikeTheTech virtual display driver is installed.");
            System.out.println("[INFO] Starting the pinned driver-only installation using signed NefCon.");
            System.out.println("[INFO] Windows may request administrator approval; no setup wizard will be launched.");
            System.out.println("[INFO] Installing the signed driver-only package...");
            if (!FastMonitor.installDriver()) {
                System.err.println("[ERROR] Driver installation was cancelled or failed. Demo stopped.");
                return;
            }
        }

        System.out.println("[INFO] Initializing FastMonitor backend...");
        boolean initialized = FastMonitor.init();
        System.out.println("[INFO] Backend initialized: " + initialized);
        if (!initialized) {
            System.err.println("[ERROR] FastMonitor could not initialize the virtual display backend.");
            FastMonitor.shutdown();
            boolean deviceRemoved = FastMonitor.removeDriverDevice();
            if (deviceRemoved) {
                System.out.println("[INFO] VDD device node removed after failed initialization; driver package retained.");
            } else {
                System.err.println("[WARN] VDD device node could not be removed after failed initialization.");
            }
            return;
        }

        boolean emulation = FastMonitor.isEmulationMode();
        if (emulation) {
            System.out.println("[WARN] Running in EMULATION MODE — no VDD driver found.");
            System.out.println("[WARN] Monitors will be tracked in memory only (no real Windows display).");
            System.out.println("[WARN] This demo requires the signed virtual display driver for a real display.");
            FastMonitor.shutdown();
            return;
        } else {
            System.out.println("[INFO] Hardware mode active — VDD driver present ✓");
        }
        System.out.println();

        Config initialConfig = new Config(1920, 1080, 60, "FastMonitor-Virtual-1");
        System.out.println("[INFO] Creating virtual monitor: " + initialConfig);

        try (FastMonitor monitor = FastMonitor.create(initialConfig)) {
            System.out.println("[SUCCESS] Virtual monitor created with ID: " + monitor.id());

            boolean activated = monitor.activate();
            System.out.println("[INFO] Monitor active in internal state model: " + activated);

            System.out.println("[INFO] Current monitors dump:");
            System.out.println(FastMonitor.dumpAllMonitorsJson());

            System.out.println();
            System.out.println(">> Monitor is currently ACTIVE. Open Windows Display Settings to inspect!");
            System.out.println(">> Press ENTER to test dynamic resolution/refresh rate change...");
            System.in.read();

            System.out.println("[INFO] Reconfiguring monitor to 2560x1440 @ 144Hz...");
            Config gamingConfig = new Config(2560, 1440, 144, "FastMonitor-Virtual-1-Gaming");
            boolean reconfigured = monitor.reconfigure(gamingConfig);
            System.out.println("[INFO] Reconfiguration successful: " + reconfigured);
            System.out.println("[INFO] Updated config: " + monitor.config());

            System.out.println("[INFO] Updated monitors dump:");
            System.out.println(FastMonitor.dumpAllMonitorsJson());

            System.out.println();
            System.out.println(">> Reconfigured to 2560x1440 @ 144Hz. Press ENTER to deactivate and close...");
            System.in.read();

            System.out.println();
            System.out.println("[INFO] Deactivating monitor...");
            monitor.deactivate();

            System.out.println("[INFO] Removing the temporary virtual display...");
            boolean removed = monitor.destroy();
            if (removed) {
                System.out.println("[SUCCESS] Temporary virtual display removed.");
            } else {
                System.err.println("[WARN] FastMonitor could not confirm display removal; shutdown will retry cleanup.");
            }
        } catch (Exception e) {
            System.err.println("[ERROR] Failed to manage virtual monitor: " + e.getMessage());
            e.printStackTrace();
        } finally {
            FastMonitor.shutdown();
            System.out.println("[INFO] FastMonitor backend shut down cleanly.");
            boolean deviceRemoved = FastMonitor.removeDriverDevice();
            if (deviceRemoved) {
                System.out.println("[SUCCESS] VDD device node removed; the driver package remains installed.");
            } else {
                System.err.println("[WARN] VDD device node could not be removed. See native diagnostics above.");
            }
        }

        System.out.println();
        System.out.println("=================================================");
        System.out.println("               Demo completed!                   ");
        System.out.println("=================================================");
    }
}
