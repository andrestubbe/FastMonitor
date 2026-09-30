package fastmonitor;

import java.util.Objects;
import java.util.HexFormat;
import java.util.Collection;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.time.Duration;

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

    private static final String VDD_ARCHIVE_SHA256 = "E24210692B442B39AF763536330CE78B423F19342B7A7792C26DE3944E418B3A";
    private static final String NEFCON_ARCHIVE_SHA256 = "A15557DA24A9EFCA203158DE3B43B0EAF982DB231F0194031F1ED428BC13E669";
    private static final String VDD_PUBLISHER_SHA1 = "3CF8CF26D8BA266C3A483AB7D26D4A818E317D76";
    private static final String VDD_ARCHIVE_URL = "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/25.7.23/VirtualDisplayDriver-x86.Driver.Only.zip";
    private static final String NEFCON_ARCHIVE_URL = "https://github.com/nefarius/nefcon/releases/download/v1.14.0/nefcon_v1.14.0.zip";
    private static final long MAX_INSTALL_ARCHIVE_BYTES = 32L * 1024L * 1024L;
    private static final HttpClient DOWNLOAD_CLIENT = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(20))
            .followRedirects(HttpClient.Redirect.NORMAL)
            .build();

    private static final AtomicBoolean INITIALIZED = new AtomicBoolean(false);

    private static void logTiming(String operation, long startedAtNanos) {
        long elapsedMillis = (System.nanoTime() - startedAtNanos) / 1_000_000L;
        System.out.println("[FastMonitor][TIMING] " + operation + ": " + elapsedMillis + " ms.");
    }

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
     * <p>This does not install a driver. Call {@link #installDriver()} explicitly
     * from the application's activation/setup action when installation is needed.</p>
     *
     * @return {@code true} if initialized successfully
     */
    public static boolean init() {
        long startedAt = System.nanoTime();
        if (INITIALIZED.get()) {
            logTiming("init (already initialized)", startedAt);
            return true;
        }
        synchronized (FastMonitor.class) {
            if (INITIALIZED.get()) {
                logTiming("init (initialized concurrently)", startedAt);
                return true;
            }

            boolean ok = FastMonitorNative.initBackend();
            if (ok) {
                BACKEND_GENERATION.incrementAndGet();
                INITIALIZED.set(true);
            }
            logTiming("init", startedAt);
            return ok;
        }
    }

    /**
     * Downloads hash-pinned MikeTheTech and NefCon packages, then installs the VDD with NefCon.
     * Windows presents its normal administrator-consent prompt.
     *
     * @return {@code true} if the driver is installed and active after the call
     */
    public static boolean installDriver() {
        long installStartedAt = System.nanoTime();
        if (FastMonitorNative.isDriverPresent()) {
            logTiming("installDriver (driver already present)", installStartedAt);
            return true;
        }

        Path tempDirectory = null;
        try {
            tempDirectory = Files.createTempDirectory("fastmonitor-vdd-");
            Path driverZip = tempDirectory.resolve("VirtualDisplayDriver-x86.Driver.Only.zip");
            Path nefconZip = tempDirectory.resolve("nefcon_v1.14.0.zip");
            System.out.println("[FastMonitor] Preparing the hash-pinned MikeTheTech VDD and NefCon packages...");
            long phaseStartedAt = System.nanoTime();
            driverZip = obtainVerifiedCachedArchive(VDD_ARCHIVE_URL,
                    "VirtualDisplayDriver-x86.Driver.Only.zip", VDD_ARCHIVE_SHA256, driverZip);
            logTiming("VDD archive cache lookup / download / SHA-256", phaseStartedAt);
            phaseStartedAt = System.nanoTime();
            nefconZip = obtainVerifiedCachedArchive(NEFCON_ARCHIVE_URL,
                    "nefcon_v1.14.0.zip", NEFCON_ARCHIVE_SHA256, nefconZip);
            logTiming("NefCon archive cache lookup / download / SHA-256", phaseStartedAt);
            phaseStartedAt = System.nanoTime();
            Path driverDirectory = tempDirectory.resolve("driver");
            extractZipEntry(driverZip, "VirtualDisplayDriver/MttVDD.inf", driverDirectory);
            extractZipEntry(driverZip, "VirtualDisplayDriver/MttVDD.dll", driverDirectory);
            Path catalog = extractZipEntry(driverZip, "VirtualDisplayDriver/mttvdd.cat", driverDirectory);
            Path settingsSource = extractZipEntry(driverZip, "VirtualDisplayDriver/vdd_settings.xml", driverDirectory);
            Path publisherCertificate = extractCatalogPublisher(catalog, tempDirectory.resolve("vdd-publisher.cer"));
            Path nefcon = extractZipEntry(nefconZip, "x64/nefconw.exe", tempDirectory.resolve("nefcon"));

            String settings = Files.readString(settingsSource, StandardCharsets.UTF_8);
            Matcher count = Pattern.compile("<count>\\s*\\d+\\s*</count>").matcher(settings);
            if (!count.find()) {
                throw new java.io.IOException("The downloaded VDD settings file has no monitor count.");
            }
            Path zeroMonitorSettings = tempDirectory.resolve("vdd_settings.xml");
            Files.writeString(zeroMonitorSettings, count.replaceFirst("<count>0</count>"), StandardCharsets.UTF_8);

            Files.writeString(tempDirectory.resolve("install-vdd.cmd"), createInstallerCommand(), StandardCharsets.US_ASCII);
            logTiming("extract and prepare signed installer", phaseStartedAt);

            System.out.println("[FastMonitor] Installing through Windows certutil and signed NefCon (no PowerShell).");
            phaseStartedAt = System.nanoTime();
            boolean ok = FastMonitorNative.installDriver(
                    tempDirectory.resolve("install-vdd.cmd").toString(), tempDirectory.toString());
            logTiming("elevated driver installation (includes Windows approval)", phaseStartedAt);
            if (ok) System.out.println("[FastMonitor] Virtual display driver installed.");
            else {
                Path systemLog = Path.of("C:\\ProgramData\\FastMonitor\\FastMonitor-vdd-install.log");
                if (Files.isRegularFile(systemLog)) {
                    Path retainedLog = Path.of(System.getProperty("java.io.tmpdir"),
                            "FastMonitor-vdd-install.log");
                    try {
                        Files.copy(systemLog, retainedLog, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                        System.err.println("[FastMonitor] Installer log: " + retainedLog);
                    } catch (java.io.IOException e) {
                        System.err.println("[FastMonitor] Could not preserve installer log: " + e.getMessage());
                    }
                }
                System.err.println("[FastMonitor] Driver installation was cancelled or failed.");
            }
            return ok;
        } catch (java.io.IOException | NoSuchAlgorithmException e) {
            System.err.println("[FastMonitor] Driver package verification or extraction failed: " + e.getMessage());
            return false;
        } finally {
            if (tempDirectory != null) {
                long cleanupStartedAt = System.nanoTime();
                deleteInstallTempDirectory(tempDirectory);
                logTiming("installer temporary-file cleanup", cleanupStartedAt);
            }
            logTiming("installDriver total", installStartedAt);
        }
    }

    private static String createInstallerCommand() {
        return new StringBuilder()
                .append("@echo off\r\n")
                .append("setlocal DisableDelayedExpansion\r\n")
                .append("set \"ROOT=%~dp0\"\r\n")
                .append("if not exist \"C:\\ProgramData\\FastMonitor\" mkdir \"C:\\ProgramData\\FastMonitor\" >nul 2>&1\r\n")
                .append("set \"LOG=C:\\ProgramData\\FastMonitor\\FastMonitor-vdd-install.log\"\r\n")
                .append("set \"RC=0\"\r\n")
                .append("> \"%LOG%\" echo FastMonitor VDD driver-only installation started.\r\n")
                .append("if exist \"C:\\VirtualDisplayDriver\\vdd_settings.xml\" goto settings_ready\r\n")
                .append("if not exist \"C:\\VirtualDisplayDriver\" mkdir \"C:\\VirtualDisplayDriver\" >> \"%LOG%\" 2>&1\r\n")
                .append("set \"RC=%ERRORLEVEL%\"\r\n")
                .append("if not \"%RC%\"==\"0\" goto failed\r\n")
                .append("copy /y \"%ROOT%vdd_settings.xml\" \"C:\\VirtualDisplayDriver\\vdd_settings.xml\" >> \"%LOG%\" 2>&1\r\n")
                .append("set \"RC=%ERRORLEVEL%\"\r\n")
                .append("if not \"%RC%\"==\"0\" goto failed\r\n")
                .append(":settings_ready\r\n")
                .append("certutil.exe -addstore -f TrustedPublisher \"%ROOT%vdd-publisher.cer\" >> \"%LOG%\" 2>&1\r\n")
                .append("set \"RC=%ERRORLEVEL%\"\r\n")
                .append("if not \"%RC%\"==\"0\" goto failed\r\n")
                .append("\"%ROOT%nefcon\\nefconw.exe\" install \"%ROOT%driver\\MttVDD.inf\" \"Root\\MttVDD\" --no-duplicates >> \"%LOG%\" 2>&1\r\n")
                .append("set \"RC=%ERRORLEVEL%\"\r\n")
                .append("if not \"%RC%\"==\"0\" goto failed\r\n")
                .append(">> \"%LOG%\" echo Driver installation command completed successfully.\r\n")
                .append("exit /b 0\r\n")
                .append(":failed\r\n")
                .append(">> \"%LOG%\" echo Installation failed with exit code %RC%.\r\n")
                .append("exit /b %RC%\r\n")
                .toString();
    }

    private static Path extractZipEntry(Path archive, String entryName, Path destinationRoot)
            throws java.io.IOException {
        Files.createDirectories(destinationRoot);
        try (InputStream input = Files.newInputStream(archive);
             ZipInputStream zip = new ZipInputStream(input)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entryName.equals(entry.getName().replace('\\', '/'))) {
                    String fileName = entryName.substring(entryName.lastIndexOf('/') + 1);
                    Path destination = destinationRoot.resolve(fileName).normalize();
                    if (!destination.startsWith(destinationRoot.toAbsolutePath().normalize())) {
                        throw new java.io.IOException("Unsafe path in pinned installer archive.");
                    }
                    try (OutputStream output = Files.newOutputStream(destination)) {
                        zip.transferTo(output);
                    }
                    zip.closeEntry();
                    return destination;
                }
                zip.closeEntry();
            }
        }
        throw new java.io.IOException("Required file is missing from pinned archive: " + entryName);
    }

    private static Path obtainVerifiedCachedArchive(String url, String fileName, String expectedSha256,
                                                     Path fallbackDestination)
            throws java.io.IOException, NoSuchAlgorithmException {
        String localAppData = System.getenv("LOCALAPPDATA");
        Path cacheDirectory = localAppData == null || localAppData.isBlank()
                ? Path.of(System.getProperty("user.home"), ".fastmonitor", "cache")
                : Path.of(localAppData, "FastMonitor", "cache");
        Path cachedArchive = cacheDirectory.resolve(fileName);

        try {
            Files.createDirectories(cacheDirectory);
        } catch (java.io.IOException e) {
            System.err.println("[FastMonitor] Archive cache is unavailable; downloading to a temporary folder: "
                    + e.getMessage());
            downloadVerifiedArchive(url, fallbackDestination, expectedSha256);
            return fallbackDestination;
        }

        if (Files.isRegularFile(cachedArchive)) {
            try {
                if (sha256Matches(cachedArchive, expectedSha256)) {
                    System.out.println("[FastMonitor] Using SHA-256-verified cached package: " + fileName);
                    return cachedArchive;
                }
                System.err.println("[FastMonitor] Cached package hash mismatch; replacing it: " + fileName);
                Files.deleteIfExists(cachedArchive);
            } catch (java.io.IOException e) {
                System.err.println("[FastMonitor] Could not verify cached package; downloading a fresh copy: "
                        + e.getMessage());
                try { Files.deleteIfExists(cachedArchive); } catch (java.io.IOException ignored) { }
            }
        }

        Path stagedArchive;
        try {
            stagedArchive = Files.createTempFile(cacheDirectory, fileName + ".", ".part");
        } catch (java.io.IOException e) {
            System.err.println("[FastMonitor] Archive cache is not writable; downloading to a temporary folder: "
                    + e.getMessage());
            downloadVerifiedArchive(url, fallbackDestination, expectedSha256);
            return fallbackDestination;
        }

        try {
            downloadVerifiedArchive(url, stagedArchive, expectedSha256);
            try {
                try {
                    Files.move(stagedArchive, cachedArchive,
                            java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                            java.nio.file.StandardCopyOption.ATOMIC_MOVE);
                } catch (java.nio.file.AtomicMoveNotSupportedException e) {
                    Files.move(stagedArchive, cachedArchive,
                            java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                }
                System.out.println("[FastMonitor] Downloaded and SHA-256-verified package cached: " + fileName);
                return cachedArchive;
            } catch (java.io.IOException e) {
                System.err.println("[FastMonitor] Could not save package in cache; using the verified temporary copy: "
                        + e.getMessage());
                Files.copy(stagedArchive, fallbackDestination,
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                return fallbackDestination;
            }
        } finally {
            Files.deleteIfExists(stagedArchive);
        }
    }

    private static boolean sha256Matches(Path archive, String expectedSha256)
            throws java.io.IOException, NoSuchAlgorithmException {
        if (Files.size(archive) > MAX_INSTALL_ARCHIVE_BYTES) {
            return false;
        }
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = Files.newInputStream(archive)) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                digest.update(buffer, 0, read);
            }
        }
        return expectedSha256.equals(HexFormat.of().withUpperCase().formatHex(digest.digest()));
    }

    private static Path extractCatalogPublisher(Path catalog, Path destination)
            throws java.io.IOException, NoSuchAlgorithmException {
        Collection<? extends Certificate> certificates;
        try (InputStream input = Files.newInputStream(catalog)) {
            certificates = CertificateFactory.getInstance("X.509").generateCertificates(input);
        } catch (java.security.cert.CertificateException e) {
            throw new java.io.IOException("Could not read the signed VDD catalog certificates.", e);
        }
        MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
        for (Certificate certificate : certificates) {
            if (certificate instanceof X509Certificate x509) {
                try {
                    byte[] encoded = x509.getEncoded();
                    String thumbprint = HexFormat.of().withUpperCase().formatHex(sha1.digest(encoded));
                    if (VDD_PUBLISHER_SHA1.equals(thumbprint)) {
                        Files.write(destination, encoded);
                        return destination;
                    }
                } catch (java.security.cert.CertificateEncodingException e) {
                    throw new java.io.IOException("Could not encode the VDD publisher certificate.", e);
                }
            }
        }
        throw new java.io.IOException("The pinned VDD publisher certificate was not found in its catalog.");
    }

    private static void downloadVerifiedArchive(String url, Path destination, String expectedSha256)
            throws java.io.IOException, NoSuchAlgorithmException {
        HttpRequest request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofMinutes(2))
                .header("User-Agent", "FastMonitor")
                .GET()
                .build();

        HttpResponse<InputStream> response;
        try {
            response = DOWNLOAD_CLIENT.send(request, HttpResponse.BodyHandlers.ofInputStream());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new java.io.IOException("Interrupted while downloading the pinned driver packages.", e);
        }
        if (response.statusCode() != 200) {
            try (InputStream ignored = response.body()) { }
            throw new java.io.IOException("Package download failed with HTTP status " + response.statusCode()
                    + ": " + url);
        }

        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long totalBytes = 0;
        try (InputStream input = response.body(); OutputStream output = Files.newOutputStream(destination)) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                totalBytes += read;
                if (totalBytes > MAX_INSTALL_ARCHIVE_BYTES) {
                    throw new java.io.IOException("Downloaded driver package exceeds the size limit.");
                }
                digest.update(buffer, 0, read);
                output.write(buffer, 0, read);
            }
        } catch (java.io.IOException e) {
            Files.deleteIfExists(destination);
            throw e;
        }

        String actualSha256 = HexFormat.of().withUpperCase().formatHex(digest.digest());
        if (!expectedSha256.equals(actualSha256)) {
            Files.deleteIfExists(destination);
            throw new java.io.IOException("SHA-256 mismatch for downloaded package: " + url);
        }
    }

    private static void deleteInstallTempDirectory(Path directory) {
        try (java.util.stream.Stream<Path> paths = Files.walk(directory)) {
            paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                try { Files.deleteIfExists(path); } catch (java.io.IOException ignored) {}
            });
        } catch (java.io.IOException ignored) {
            // Temporary files are not part of the installed driver.
        }
    }

    /**
     * Returns {@code true} if FastMonitor is running in emulation mode
     * (no VDD driver installed — monitors are tracked in memory only).
     */
    public static boolean isEmulationMode() {
        return FastMonitorNative.isEmulationMode();
    }

    /**
     * Shuts down the native FastMonitor backend.
     */
    public static synchronized void shutdown() {
        long startedAt = System.nanoTime();
        if (!INITIALIZED.get()) {
            logTiming("shutdown (already stopped)", startedAt);
            return;
        }
        FastMonitorNative.shutdownBackend();
        INITIALIZED.set(false);
        logTiming("shutdown", startedAt);
    }

    /**
     * Removes the MikeTheTech VDD device node while retaining its driver package.
     * Call only after {@link #shutdown()} and after all virtual monitors are destroyed.
     *
     * @return {@code true} if the device node is absent after the call
     */
    public static boolean removeDriverDevice() {
        if (INITIALIZED.get()) {
            throw new IllegalStateException("Shut down FastMonitor before removing its driver device.");
        }
        long startedAt = System.nanoTime();
        boolean removed = FastMonitorNative.removeDriverDevice();
        logTiming("removeDriverDevice", startedAt);
        return removed;
    }

    /**
     * Checks if the MikeTheTech VDD driver is present on this system.
     */
    public static boolean isDriverPresent() {
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
     * @throws IllegalStateException if backend initialization, display creation, or mode application fails
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
     *
     * @return {@code true} only if Windows accepted and applied the mode; otherwise
     *         the previous configuration is retained
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
     *
     * @return {@code true} only if Windows accepted and applied the mode; otherwise
     *         the previous configuration is retained
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
            // Note: mode is reconfigured; name is retained from original creation
            this.config = new Config(newConfig.width, newConfig.height, newConfig.refreshHz, cur.name);
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
     * In hardware mode, destroy monitors in reverse creation order because
     * MttVDD only supports removing its highest-numbered slot. An out-of-order
     * call returns {@code false} and leaves this monitor open.
     */
    public boolean destroy() {
        if (!state.compareAndSet(STATE_OPEN, STATE_DESTROYING)) {
            return false;
        }
        try {
            if (!INITIALIZED.get() || generation != BACKEND_GENERATION.get()) {
                state.set(STATE_CLOSED);
                return false;
            }
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
        if (!destroy() && state.get() == STATE_OPEN) {
            throw new IllegalStateException(
                    "Could not destroy this monitor. In hardware mode, close monitors in reverse creation order.");
        }
    }

    /**
     * Returns a JSON representation of all currently active virtual monitors.
     */
    public static String dumpAllMonitorsJson() {
        if (!INITIALIZED.get()) {
            return "[]";
        }
        return FastMonitorNative.listVirtualMonitorsJson();
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
