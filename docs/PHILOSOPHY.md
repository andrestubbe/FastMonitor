# The Philosophy of FastMonitor 💡

> [!IMPORTANT]
> **"Echte Hardware-Outputs. Sub-Mikrosekunden-Latenz. Zero GC. Native-First Display Architecture."**

FastMonitor is built on the conviction that dynamic display topologies, headless rendering, and virtual workspace expansion in Java should never be restricted to fake window streams, slow virtual desktop hacks, or fragile OS workarounds.

---

## Core Tenets

### 1. True OS-Level Virtual Displays
Standard Java has no concept of dynamically adding monitors to the operating system. FastMonitor changes this fundamentally by speaking directly to the Windows Indirect Display Driver (IDD / IddCx) subsystem and virtual display device drivers. Created monitors appear to Windows, DirectX, Vulkan, and DXGI as genuine physical hardware displays.

### 2. High-Frequency Real-Time Performance
Whether for autonomous agent test sandboxes, high-refresh multi-display simulation, or offscreen GPU rendering pipelines, FastMonitor supports up to 8K resolutions and arbitrary refresh rates from 60 Hz to 500 Hz with deterministic sub-millisecond execution times.

### 3. Direct Windows PnP Control
Monitor creation, configuration, and teardown operate through the MikeTheTech VDD settings file and Windows PnP APIs. Java keeps the public lifecycle small while the native bridge handles device restarts and display-mode changes.

### 4. Seamless Synergy with the FastJava Ecosystem
*   **FastScreen**: Captures virtual monitors at up to 2000 FPS without interfering with physical user monitors.
*   **FastGPU & FastVulkan**: Target virtual displays with dedicated swapchains and offscreen render passes.
*   **FastRobot**: Injects autonomous mouse clicks and keystrokes into isolated virtual screens without stealing physical input focus.
*   **FastCore**: Powers seamless native binary deployment with zero external dependencies.

### 5. Predictable Temporary-Display Cleanup
The demo clears the VDD monitor count, shuts down its backend, and removes the MttVDD device node on exit. It keeps the driver package installed so a later run can recreate the device without launching the vendor setup wizard.

### 6. Streamer & Broadcast Privacy Isolation
Live broadcasters, content creators, and corporate presenters require guaranteed isolation between what is visible on stream and what remains private. FastMonitor provides a dedicated, untainted virtual output surface for OBS Studio and capture cards, eliminating accidental leaks of Discord notifications, private chat tabs, or password dialogs.

---

**⚡ FastMonitor — Powering the next generation of Native Java Display Management.**
