# FastMonitor Roadmap 🗺️

Future technical trajectory and milestones for **FastMonitor**.

---

## Phase 1: Core Foundation (Completed — v0.1.0)
- [x] Basic Parsec-VDD / IDD JNI bridge via Win32 `DeviceIoControl`.
- [x] Java 17 high-level API wrapper with `AutoCloseable` lifecycle.
- [x] Background keepalive watchdog thread.
- [x] Dynamic resolution and refresh rate reconfiguration.
- [x] Automated MSVC build script with FastCore deployment.

---

## Phase 2: Topology & EDID Customization (v0.2.0)
- [ ] Custom EDID generation (HDR10, wide-gamut DCI-P3 color spaces).
- [ ] Explicit virtual monitor positioning (`SetDisplayConfig` coordinates).
- [ ] Multi-adapter routing (binding virtual displays to specific discrete GPUs).
- [ ] Custom orientation support (Portrait, Landscape Flipped).

---

## Phase 3: FastJava Ecosystem Synergy (v0.3.0)
- [ ] Direct DXGI output index binding with `FastScreen` (zero-overhead screen recording of headless displays).
- [ ] `FastGPU` / `FastVulkan` automatic swapchain creation targeting `FastMonitor` outputs.
- [ ] `FastRobot` virtual desktop sandboxing (isolated input injection without moving physical cursor).
