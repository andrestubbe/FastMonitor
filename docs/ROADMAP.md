# FastMonitor Roadmap 🗺️

Future technical trajectory and milestones for **FastMonitor**.

---

## Phase 1: Core Foundation (Completed — v0.1.0)
- [x] Hardened Parsec-VDD / IDD JNI bridge via Win32 `DeviceIoControl`.
- [x] Payload-free `VDD_IOCTL_ADD` and 16-bit big-endian `VDD_IOCTL_REMOVE`.
- [x] High-precision 100 ms keepalive thread without handle leaks.
- [x] Java 17 high-level API wrapper with `AutoCloseable` and atomic lifecycle.
- [x] Win32 CCD (`ChangeDisplaySettingsExA`) resolution and refresh rate mode configuration.
- [x] Primitive zero-allocation `reconfigure(int, int, int)` hot-path.
- [x] Automated MSVC build script with FastCore deployment.

---

## Phase 2: Topology & EDID Customization (v0.2.0)
- [ ] Custom EDID generation (HDR10, wide-gamut DCI-P3 color spaces).
- [ ] Explicit virtual monitor positioning (`SetDisplayConfig` coordinates).
- [ ] Multi-adapter routing (binding virtual displays to specific discrete GPUs).
- [ ] Custom orientation support (Portrait, Landscape Flipped).
- [ ] Optional Java 22+ Foreign Function & Memory (FFM) direct downcall bindings.

---

## Phase 3: FastJava Ecosystem Synergy (v0.3.0)
- [ ] Direct DXGI output index binding with `FastScreen` (zero-overhead screen recording of headless displays).
- [ ] `FastGPU` / `FastVulkan` automatic swapchain creation targeting `FastMonitor` outputs.
- [ ] `FastRobot` virtual desktop sandboxing (isolated input injection without moving physical cursor).
