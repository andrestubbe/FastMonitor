# FastMonitor 0.1.0 — Initial Release

FastMonitor is a Java 17 library for creating, configuring, activating, and removing virtual displays on Windows through the MikeTheTech Virtual Display Driver (MttVDD).

## What's Included

- Java API for virtual monitor creation, activation, deactivation, reconfiguration, and cleanup.
- MttVDD integration using its settings file and Windows Plug and Play APIs.
- Driver setup downloads the pinned driver-only MttVDD and signed NefCon packages, verifies both archives with SHA-256, and uses normal Windows administrator approval.
- The interactive demo creates a real Windows display output, pauses so it can be inspected in Display Settings, and removes its temporary device node on exit. The installed driver package remains available for later use.
- Library users can use software-emulation mode when no VDD device is installed; this mode tracks monitors in memory and does not create Windows display outputs.

## Requirements

- Windows 10 or 11, x64
- Java 17 or later
- MikeTheTech VDD for real virtual displays; first-time setup requires an internet connection and Windows administrator approval

## Installation with JitPack

```xml
<repositories>
    <repository>
        <id>jitpack.io</id>
        <url>https://jitpack.io</url>
    </repository>
</repositories>

<dependencies>
    <dependency>
        <groupId>com.github.andrestubbe</groupId>
        <artifactId>FastMonitor</artifactId>
        <version>0.1.0</version>
    </dependency>
    <dependency>
        <groupId>com.github.andrestubbe</groupId>
        <artifactId>FastCore</artifactId>
        <version>0.1.0</version>
    </dependency>
</dependencies>
```

**Part of the FastJava Ecosystem** — *Making the JVM faster.* ⚡
