# FastMonitor Roadmap 🗺️

Future technical trajectory and milestones for **FastMonitor**.

---

## Current MVP: MikeTheTech VDD (Completed)
- [x] Native JNI bridge for MttVDD settings, Windows PnP restarts, display modes, and device removal.
- [x] Java 17 monitor API with `AutoCloseable` lifecycle and emulation fallback.
- [x] Automatic download of fixed MikeTheTech VDD and NefCon releases with SHA-256 verification.
- [x] Driver-only installation with normal Windows administrator approval and no vendor setup wizard.
- [x] Demo cleanup that removes the temporary MttVDD device node but retains the driver package.
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
