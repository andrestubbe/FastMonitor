package fastmonitor;

import fastmonitor.FastMonitor.Config;

/**
 * Visual Showcase Demo for FastMonitor.
 *
 * <p>Demonstrates creating, reconfiguring, activating, and destroying
 * virtual display monitors directly from Java.</p>
 */
public class Demo {

    private static void logTiming(String operation, long startedAtNanos) {
        long elapsedMillis = (System.nanoTime() - startedAtNanos) / 1_000_000L;
        System.out.println("[FastMonitor][TIMING] " + operation + ": " + elapsedMillis + " ms.");
    }

    public static void main(String[] args) {
        System.out.println("=================================================");
        System.out.println("       ⚡ FastMonitor Visual Showcase Demo        ");
        System.out.println("=================================================");
        System.out.println();

        long phaseStartedAt = System.nanoTime();
        boolean driverPresent = FastMonitor.isDriverPresent();
        logTiming("demo driver presence check", phaseStartedAt);
        if (!driverPresent) {
            System.out.println("[INFO] No MikeTheTech virtual display driver is installed.");
            System.out.println("[INFO] Starting the pinned driver-only installation using signed NefCon.");
            System.out.println("[INFO] Windows may request administrator approval; no setup wizard will be launched.");
            System.out.println("[INFO] Installing the signed driver-only package...");
            if (!FastMonitor.installDriver()) {
                System.err.println("[ERROR] Driver installation was cancelled or failed. Demo stopped.");
                System.exit(1);
                return;
            }
        }

        System.out.println("[INFO] Initializing FastMonitor backend...");
        phaseStartedAt = System.nanoTime();
        boolean initialized = FastMonitor.init();
        logTiming("demo backend initialization", phaseStartedAt);
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
            System.exit(1);
            return;
        }

        boolean emulation = FastMonitor.isEmulationMode();
        if (emulation) {
            System.out.println("[WARN] Running in EMULATION MODE — no VDD driver found.");
            System.out.println("[WARN] Monitors will be tracked in memory only (no real Windows display).");
            System.out.println("[WARN] This demo requires the signed virtual display driver for a real display.");
            FastMonitor.shutdown();
            System.exit(1);
            return;
        } else {
            System.out.println("[INFO] Hardware mode active — VDD driver present ✓");
        }
        System.out.println();

        Config initialConfig = new Config(1920, 1080, 60, "FastMonitor-Virtual-1");
        System.out.println("[INFO] Creating virtual monitor: " + initialConfig);

        boolean demoFailed = false;
        boolean inspectionPauseShown = false;
        try {
            phaseStartedAt = System.nanoTime();
            FastMonitor monitor = FastMonitor.create(initialConfig);
            logTiming("demo create monitor (includes VDD restart and Windows mode apply)", phaseStartedAt);
            try (monitor) {
                System.out.println("[SUCCESS] Virtual monitor created with ID: " + monitor.id());

                boolean activated = monitor.activate();
                System.out.println("[INFO] Monitor active in internal state model: " + activated);

                System.out.println("[INFO] Current monitors dump:");
                System.out.println(FastMonitor.dumpAllMonitorsJson());

                System.out.println();
                System.out.println(">> Monitor is ACTIVE. Open Windows Display Settings to inspect it.");
                System.out.println(">> Press ENTER once to remove the temporary virtual display and exit...");
                inspectionPauseShown = true;
                System.in.read();

                System.out.println();
                System.out.println("[INFO] Deactivating monitor...");
                phaseStartedAt = System.nanoTime();
                monitor.deactivate();
                logTiming("demo deactivate monitor state", phaseStartedAt);

                System.out.println("[INFO] Removing the temporary virtual display...");
                phaseStartedAt = System.nanoTime();
                boolean removed = monitor.destroy();
                logTiming("demo destroy monitor (includes VDD restart)", phaseStartedAt);
                if (removed) {
                    System.out.println("[SUCCESS] Temporary virtual display removed.");
                } else {
                    System.err.println("[WARN] FastMonitor could not confirm display removal; shutdown will retry cleanup.");
                    demoFailed = true;
                }
            }
        } catch (Exception e) {
            System.err.println("[ERROR] Failed to manage virtual monitor: " + e.getMessage());
            e.printStackTrace();
            demoFailed = true;
            if (!inspectionPauseShown) {
                System.out.println("[INFO] If a temporary display appeared, inspect it now; it will be cleaned up after ENTER.");
                System.out.println(">> Press ENTER once to clean up and exit...");
                inspectionPauseShown = true;
                try {
                    System.in.read();
                } catch (java.io.IOException inputError) {
                    System.err.println("[WARN] Could not wait for ENTER: " + inputError.getMessage());
                }
            }
        } finally {
            phaseStartedAt = System.nanoTime();
            FastMonitor.shutdown();
            logTiming("demo backend shutdown", phaseStartedAt);
            System.out.println("[INFO] FastMonitor backend shut down cleanly.");
            boolean deviceRemoved = FastMonitor.removeDriverDevice();
            if (deviceRemoved) {
                System.out.println("[SUCCESS] VDD device node removed; the driver package remains installed.");
            } else {
                System.err.println("[WARN] VDD device node could not be removed. See native diagnostics above.");
            }
        }

        if (demoFailed) {
            System.err.println("[ERROR] Demo did not complete successfully.");
            System.exit(2); // The demo already provided its inspection pause.
        }
        System.out.println();
        System.out.println("=================================================");
        System.out.println("               Demo completed!                   ");
        System.out.println("=================================================");
    }
}
