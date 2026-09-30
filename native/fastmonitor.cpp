/**
 * fastmonitor.cpp  —  MikeTheTech VDD (VirtualDrivers) Backend
 *
 * Strategy:
 *   - Driver install  : hash-pinned driver-only package + NefCon, behind UAC
 *   - Monitor control : modify C:\VirtualDisplayDriver\vdd_settings.xml
 *                       then CM_Disable_DevNode + CM_Enable_DevNode
 *   - Display query   : EnumDisplayDevices to find the new Windows display
 *
 * Admin rights required: CM_Disable/Enable_DevNode need elevated privileges.
 */

#include "fastmonitor.h"

#include <windows.h>
#include <shellapi.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <newdev.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>

class ScopedTiming {
public:
    explicit ScopedTiming(const char* operation)
        : operation_(operation), start_(std::chrono::steady_clock::now()) {}
    ~ScopedTiming() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_).count();
        std::fprintf(stderr, "[FastMonitor][TIMING] %s: %lld ms.\n",
                operation_, static_cast<long long>(elapsed));
    }
private:
    const char* operation_;
    std::chrono::steady_clock::time_point start_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────
static std::mutex              g_mutex;          // protects g_monitors, g_nextLogicalId
static std::mutex              g_backendOpMutex; // serialises init/shutdown/create/destroy
static std::vector<FastVirtualMonitor> g_monitors;
static std::atomic<int>        g_nextLogicalId{1};
static std::atomic<bool>       g_initialized{false};
static std::atomic<bool>       g_emulationMode{false}; // true when no driver installed
static DEVINST                 g_devInst = 0;    // PnP device instance handle

// ─────────────────────────────────────────────────────────────────────────────
// XML helpers  (no external XML lib — plain string manipulation)
// ─────────────────────────────────────────────────────────────────────────────

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static bool write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << content;
    return f.good();
}

/** Read <count>N</count> from vdd_settings.xml. Returns -1 on error. */
static int xml_read_count(const std::string& xml) {
    const std::string tag_open  = "<count>";
    const std::string tag_close = "</count>";
    auto pos = xml.find(tag_open);
    if (pos == std::string::npos) return -1;
    pos += tag_open.size();
    auto end = xml.find(tag_close, pos);
    if (end == std::string::npos) return -1;
    try { return std::stoi(xml.substr(pos, end - pos)); }
    catch (...) { return -1; }
}

/** Replace <count>N</count> with <count>newVal</count>. */
static std::string xml_write_count(const std::string& xml, int newVal) {
    const std::string tag_open  = "<count>";
    const std::string tag_close = "</count>";
    auto pos = xml.find(tag_open);
    if (pos == std::string::npos) return xml;
    auto end = xml.find(tag_close, pos + tag_open.size());
    if (end == std::string::npos) return xml;
    return xml.substr(0, pos + tag_open.size())
         + std::to_string(newVal)
         + xml.substr(end);
}

static int read_monitor_count() {
    std::string xml = read_file(VDD_SETTINGS_XML);
    if (xml.empty()) return -1;
    return xml_read_count(xml);
}

static bool write_monitor_count(int count) {
    std::string xml = read_file(VDD_SETTINGS_XML);
    if (xml.empty()) return false;
    std::string updated = xml_write_count(xml, count);
    return write_file(VDD_SETTINGS_XML, updated);
}

// ─────────────────────────────────────────────────────────────────────────────
// PnP device helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Locate ROOT\DISPLAY\0000 (MttVDD) in the PnP tree. Returns 0 on failure. */
static DEVINST find_vdd_devinst() {
    DEVINST dn = 0;
    CONFIGRET cr = CM_Locate_DevNodeA(&dn, (DEVINSTID_A)VDD_INSTANCE_ID, CM_LOCATE_DEVNODE_NORMAL);
    if (cr != CR_SUCCESS) return 0;
    return dn;
}

/** Restart the VDD using Windows' supported PnPUtil command. */
static bool restart_vdd_with_pnputil() {
    char systemDirectory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryA(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        std::fprintf(stderr, "[FastMonitor] Could not locate the Windows system directory (error %lu).\n",
                GetLastError());
        return false;
    }

    const std::string executable = std::string(systemDirectory) + "\\pnputil.exe";
    std::string commandLine = "\"" + executable + "\" /restart-device \"" + VDD_INSTANCE_ID + "\"";
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process = {};
    if (!CreateProcessA(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "[FastMonitor] Could not start PnPUtil (Win32 error %lu).\n", GetLastError());
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 60000);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
        TerminateProcess(process.hProcess, error);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::fprintf(stderr, "[FastMonitor] PnPUtil device restart did not finish (Win32 error %lu).\n", error);
        return false;
    }

    DWORD exitCode = 1;
    const BOOL gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
    const DWORD exitError = gotExitCode ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!gotExitCode) {
        std::fprintf(stderr, "[FastMonitor] PnPUtil could not restart MttVDD (exit code %lu).\n",
                exitError);
        return false;
    }
    if (exitCode == ERROR_SUCCESS_REBOOT_REQUIRED) {
        std::fprintf(stderr,
                "[FastMonitor] PnPUtil completed the device restart but reports that Windows may need a reboot; checking the current device state.\n");
        return true;
    }
    if (exitCode != ERROR_SUCCESS) {
        std::fprintf(stderr, "[FastMonitor] PnPUtil could not restart MttVDD (exit code %lu).\n", exitCode);
        return false;
    }
    return true;
}

/** Restart the VDD device node to pick up XML changes, then wait until it is running. */
static bool restart_vdd_device() {
    ScopedTiming timing("native.restart_vdd_device (PnPUtil plus device readiness polling)");
    g_devInst = find_vdd_devinst();
    if (g_devInst == 0) {
        std::fprintf(stderr, "[FastMonitor] Cannot restart MttVDD: device node was not found.\n");
        return false;
    }

    if (!restart_vdd_with_pnputil()) return false;

    // PnPUtil waits for the restart request. Recheck the devnode state before
    // asking Windows to apply the updated display topology.
    for (int attempt = 0; attempt < 30; ++attempt) {
        g_devInst = find_vdd_devinst();
        if (g_devInst != 0) {
            ULONG status = 0, problem = 0;
            if (CM_Get_DevNode_Status(&status, &problem, g_devInst, 0) == CR_SUCCESS &&
                (status & DN_STARTED) != 0) {
                return true;
            }
        }
        Sleep(100);
    }
    std::fprintf(stderr, "[FastMonitor] MttVDD did not return to the started state after the PnPUtil restart.\n");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Driver installation
// ─────────────────────────────────────────────────────────────────────────────

/** Check if the MttVDD device node exists and is running. */
static bool is_driver_installed() {
    DEVINST dn = find_vdd_devinst();
    if (dn == 0) return false;
    ULONG status = 0, problem = 0;
    CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, dn, 0);
    return (cr == CR_SUCCESS);
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows display enumeration
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Find the Windows display device name (e.g. "\\.\DISPLAY2") for a VDD
 * monitor at a given slot index (0-based).
 * VDD monitors have description containing "VDD" or "MTT".
 */
static std::string find_display_for_vdd_slot(int slotIndex) {
    std::vector<std::string> vddDisplays;
    DISPLAY_DEVICEA dd = {};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            std::string desc = dd.DeviceString;
            // MikeTheTech VDD monitors show "Virtual Display Driver" or similar
            if (desc.find("MTT") != std::string::npos ||
                desc.find("VDD") != std::string::npos ||
                desc.find("Virtual Display") != std::string::npos ||
                desc.find("MikeTheTech") != std::string::npos) {
                vddDisplays.push_back(dd.DeviceName);
            }
        }
        dd = {};
        dd.cb = sizeof(dd);
    }
    if (slotIndex < (int)vddDisplays.size())
        return vddDisplays[slotIndex];
    return "";
}

/**
 * A PnP restart can report completion before Windows publishes the updated
 * display topology through EnumDisplayDevices. Wait briefly for the newly
 * added slot instead of immediately rolling back a valid XML change.
 */
static std::string wait_for_display_for_vdd_slot(int slotIndex) {
    ScopedTiming timing("native.wait_for_vdd_display_enumeration");
    constexpr int attempts = 20;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        std::string deviceName = find_display_for_vdd_slot(slotIndex);
        if (!deviceName.empty()) {
            if (attempt > 0) {
                std::fprintf(stderr,
                        "[FastMonitor] VDD display for slot %d appeared after %d ms.\n",
                        slotIndex, attempt * 100);
            }
            return deviceName;
        }
        if (attempt + 1 < attempts) Sleep(100);
    }

    std::fprintf(stderr,
            "[FastMonitor] VDD display for slot %d was not enumerated after %d ms; attached display devices were:\n",
            slotIndex, (attempts - 1) * 100);
    DISPLAY_DEVICEA dd = {};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {
        std::fprintf(stderr, "[FastMonitor]   %s | %s | flags=0x%08lx\n",
                dd.DeviceName, dd.DeviceString, dd.StateFlags);
        dd = {};
        dd.cb = sizeof(dd);
    }
    return "";
}

/** Apply resolution/refresh to a named display device. */
static bool apply_display_mode(const std::string& deviceName, int w, int h, int hz) {
    ScopedTiming timing("native.apply_display_mode (Windows mode commit)");
    if (deviceName.empty()) return false;
    DEVMODEA dm = {};
    dm.dmSize       = sizeof(dm);
    dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth  = (DWORD)w;
    dm.dmPelsHeight = (DWORD)h;
    dm.dmDisplayFrequency = (DWORD)hz;
    const LONG staged = ChangeDisplaySettingsExA(deviceName.c_str(), &dm, nullptr,
                                                  CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    if (staged != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "[FastMonitor] Could not stage mode %dx%d@%d for %s (result %ld).\n",
                w, h, hz, deviceName.c_str(), staged);
        return false;
    }

    // CDS_NORESET records this device's mode without applying it. A final
    // NULL-device call commits the staged display changes to Windows.
    const LONG applied = ChangeDisplaySettingsExA(nullptr, nullptr, nullptr, 0, nullptr);
    if (applied != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "[FastMonitor] Could not apply staged display mode for %s (result %ld).\n",
                deviceName.c_str(), applied);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON helper
// ─────────────────────────────────────────────────────────────────────────────
static std::string escape_json(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// fastmonitor namespace — public API implementation
// ─────────────────────────────────────────────────────────────────────────────
namespace fastmonitor {

bool initBackend() {
    ScopedTiming timing("native.initBackend");
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (g_initialized.load()) return true;

    // 1. Detect driver presence — installation is the Java layer's responsibility
    if (!is_driver_installed()) {
        g_emulationMode.store(true);
        g_initialized.store(true);
        return true; // emulation mode: API works but no real Windows monitors
    }

    g_emulationMode.store(false);

    // 2. Locate PnP device instance
    g_devInst = find_vdd_devinst();

    // 3. Read current monitor count from XML and sync g_monitors
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_monitors.clear();
        int count = read_monitor_count();
        if (count < 0) count = 0;
        for (int i = 0; i < count; ++i) {
            std::string devName = find_display_for_vdd_slot(i);
            g_monitors.emplace_back(g_nextLogicalId++, i, 1920, 1080, 60,
                                    "FastMonitor-Virtual-" + std::to_string(i + 1),
                                    devName);
        }
    }

    g_initialized.store(true);
    return true;
}

void shutdownBackend() {
    ScopedTiming timing("native.shutdownBackend");
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (!g_initialized.load()) return;

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        // A final cleanup pass also retries a failed per-monitor destroy.
        int count = read_monitor_count();
        if (!g_emulationMode.load() && count > 0) {
            if (!write_monitor_count(0)) {
                std::fprintf(stderr, "[FastMonitor] Shutdown cleanup could not write monitor count 0.\n");
            } else {
                if (g_devInst == 0) g_devInst = find_vdd_devinst();
                if (g_devInst == 0 || !restart_vdd_device()) {
                    std::fprintf(stderr, "[FastMonitor] Shutdown cleanup could not restart MttVDD; the temporary display may remain until the driver is restarted.\n");
                } else {
                    std::fprintf(stderr, "[FastMonitor] Shutdown cleanup removed remaining virtual displays.\n");
                }
            }
        }
        g_monitors.clear();
        g_nextLogicalId.store(1);
    }

    g_devInst = 0;
    g_initialized.store(false);
}

int createVirtualMonitor(int width, int height, int refreshHz,
                         const std::string& name) {
    ScopedTiming timing("native.createVirtualMonitor");
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (!g_initialized.load()) return -1;

    int currentCount;
    int newSlot;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        currentCount = (int)g_monitors.size();
        if (currentCount >= VDD_MAX_MONITORS) return -1;
        newSlot = currentCount;
    }

    // ── Emulation mode: no driver, just track in memory ──────────────────────
    if (g_emulationMode.load()) {
        std::lock_guard<std::mutex> lk(g_mutex);
        int lid = g_nextLogicalId++;
        g_monitors.emplace_back(lid, newSlot, width, height, refreshHz, name, "");
        return lid;
    }

    // ── Hardware mode: update XML and restart VDD device ─────────────────────
    int xmlCount = read_monitor_count();
    if (xmlCount < 0) xmlCount = currentCount;
    if (!write_monitor_count(xmlCount + 1)) return -1;

    if (!restart_vdd_device()) {
        write_monitor_count(xmlCount); // rollback
        return -1;
    }

    std::string devName = wait_for_display_for_vdd_slot(newSlot);
    if (devName.empty() || !apply_display_mode(devName, width, height, refreshHz)) {
        std::fprintf(stderr,
                "[FastMonitor] Could not create monitor in VDD slot %d: its display mode could not be applied; rolling back the added slot.\n",
                newSlot);
        if (!write_monitor_count(xmlCount)) {
            std::fprintf(stderr, "[FastMonitor] Create rollback could not restore VDD monitor count %d.\n", xmlCount);
        } else if (!restart_vdd_device()) {
            std::fprintf(stderr, "[FastMonitor] Create rollback restored the count but could not restart MttVDD; the extra display may remain until restart.\n");
        }
        return -1;
    }

    std::lock_guard<std::mutex> lk(g_mutex);
    int lid = g_nextLogicalId++;
    g_monitors.emplace_back(lid, newSlot, width, height, refreshHz, name, devName);
    return lid;
}

bool destroyVirtualMonitor(int logicalId) {
    ScopedTiming timing("native.destroyVirtualMonitor");
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (!g_initialized.load()) return false;

    FastVirtualMonitor* mon = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (auto& m : g_monitors) {
            if (m.logicalId == logicalId) { mon = &m; break; }
        }
        if (!mon) return false;
    }

    // ── Emulation mode ─────────────────────────────────────────────────────────
    if (g_emulationMode.load()) {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_monitors.erase(std::remove_if(g_monitors.begin(), g_monitors.end(),
            [logicalId](const FastVirtualMonitor& m){ return m.logicalId == logicalId; }),
            g_monitors.end());
        return true;
    }

    // ── Hardware mode ──────────────────────────────────────────────────────────
    int xmlCount = read_monitor_count();
    if (xmlCount <= 0) {
        std::fprintf(stderr, "[FastMonitor] Could not remove monitor %d: VDD settings count is %d.\n", logicalId, xmlCount);
        return false;
    }
    if (mon->slotIndex != xmlCount - 1) {
        std::fprintf(stderr,
                "[FastMonitor] Could not remove monitor %d from VDD slot %d: MttVDD only supports removing the highest slot (current last slot is %d). Destroy monitors in reverse creation order.\n",
                logicalId, mon->slotIndex, xmlCount - 1);
        return false;
    }
    if (!write_monitor_count(xmlCount - 1)) {
        std::fprintf(stderr, "[FastMonitor] Could not remove monitor %d: failed to write VDD settings.\n", logicalId);
        return false;
    }

    if (!restart_vdd_device()) {
        write_monitor_count(xmlCount); // rollback
        std::fprintf(stderr, "[FastMonitor] Could not remove monitor %d: restarting MttVDD failed; settings were rolled back.\n", logicalId);
        return false;
    }

    // MttVDD represents the active set as a count, so only the last slot can
    // be removed without recreating the remaining physical displays.
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto it = g_monitors.begin(); it != g_monitors.end(); ++it) {
        if (it->logicalId == logicalId) {
            g_monitors.erase(it);
            break;
        }
    }
    return true;
}

bool configureVirtualMonitor(int logicalId, int width, int height, int refreshHz) {
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (!g_initialized.load()) return false;

    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& m : g_monitors) {
        if (m.logicalId == logicalId) {
            if (!g_emulationMode.load()) {
                if (m.deviceName.empty()) {
                    std::fprintf(stderr, "[FastMonitor] Could not configure monitor %d: no Windows display device is mapped to VDD slot %d.\n",
                            logicalId, m.slotIndex);
                    return false;
                }
                if (!apply_display_mode(m.deviceName, width, height, refreshHz)) {
                    if (!apply_display_mode(m.deviceName, m.width, m.height, m.refreshHz)) {
                        std::fprintf(stderr, "[FastMonitor] Configure rollback could not restore the previous mode for monitor %d.\n",
                                logicalId);
                    }
                    return false;
                }
            }
            m.width     = width;
            m.height    = height;
            m.refreshHz = refreshHz;
            return true;
        }
    }
    return false;
}

bool activateVirtualMonitor(int logicalId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& m : g_monitors) {
        if (m.logicalId == logicalId) { m.active = true; return true; }
    }
    return false;
}

bool deactivateVirtualMonitor(int logicalId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& m : g_monitors) {
        if (m.logicalId == logicalId) { m.active = false; return true; }
    }
    return false;
}

std::string listVirtualMonitorsJson() {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        const auto& m = g_monitors[i];
        if (i) ss << ",";
        ss << "{\"logicalId\":" << m.logicalId
           << ",\"slotIndex\":" << m.slotIndex
           << ",\"width\":"     << m.width
           << ",\"height\":"    << m.height
           << ",\"refreshHz\":" << m.refreshHz
           << ",\"name\":\""    << escape_json(m.name) << "\""
           << ",\"deviceName\":\"" << escape_json(m.deviceName) << "\""
           << ",\"active\":"    << (m.active ? "true" : "false")
           << "}";
    }
    ss << "]";
    return ss.str();
}

int driverVersion() {
    return is_driver_installed() ? 1 : 0;
}

bool isDriverPresent() {
    return is_driver_installed();
}

static bool remove_vdd_device_with_uac() {
    wchar_t systemDir[MAX_PATH] = {};
    const UINT dirLength = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (dirLength == 0 || dirLength >= MAX_PATH) {
        std::fprintf(stderr, "[FastMonitor] Could not locate Windows System32 for elevated VDD removal (Win32 error %lu).\n",
                GetLastError());
        return false;
    }

    const std::wstring pnputil = std::wstring(systemDir) + L"\\pnputil.exe";
    const std::wstring parameters = L"/remove-device \"ROOT\\DISPLAY\\0000\"";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = pnputil.c_str();
    sei.lpParameters = parameters.c_str();
    sei.lpDirectory = systemDir;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            std::fprintf(stderr, "[FastMonitor] User cancelled Windows approval to remove the temporary VDD device.\n");
        } else {
            std::fprintf(stderr, "[FastMonitor] Could not launch elevated VDD removal (Win32 error %lu).\n", error);
        }
        return false;
    }
    if (!sei.hProcess) {
        std::fprintf(stderr, "[FastMonitor] Elevated VDD removal returned no process handle.\n");
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool gotExitCode = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    if (!gotExitCode || (exitCode != ERROR_SUCCESS && exitCode != ERROR_SUCCESS_REBOOT_REQUIRED)) {
        std::fprintf(stderr, "[FastMonitor] Elevated pnputil device removal failed (wait=%lu, exit=%lu).\n",
                waitResult, exitCode);
        return false;
    }

    for (int attempt = 0; attempt < 25; ++attempt) {
        if (find_vdd_devinst() == 0) {
            std::fprintf(stderr, "[FastMonitor] VDD device node removed with Windows administrator approval; driver package retained.\n");
            return true;
        }
        Sleep(200);
    }
    std::fprintf(stderr, "[FastMonitor] Elevated pnputil completed but the VDD device node is still present.\n");
    return false;
}

bool removeDriverDevice() {
    ScopedTiming timing("native.removeDriverDevice");
    std::lock_guard<std::mutex> opLock(g_backendOpMutex);
    if (g_initialized.load()) {
        std::fprintf(stderr, "[FastMonitor] Refusing to remove MttVDD while the backend is active.\n");
        return false;
    }

    DEVINST devInst = find_vdd_devinst();
    if (devInst == 0) {
        g_devInst = 0;
        return true; // The requested final state is already in place.
    }

    const int monitorCount = read_monitor_count();
    if (monitorCount != 0) {
        std::fprintf(stderr, "[FastMonitor] Refusing to remove MttVDD: settings contain %d monitors.\n", monitorCount);
        return false;
    }

    HDEVINFO devices = SetupDiGetClassDevsA(nullptr, nullptr, nullptr,
            DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[FastMonitor] Could not enumerate devices before MttVDD removal (Win32 error %lu).\n",
                GetLastError());
        return false;
    }

    SP_DEVINFO_DATA deviceInfo = {};
    deviceInfo.cbSize = sizeof(deviceInfo);
    if (!SetupDiOpenDeviceInfoA(devices, VDD_INSTANCE_ID, nullptr, 0, &deviceInfo)) {
        const DWORD error = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        std::fprintf(stderr, "[FastMonitor] Could not open MttVDD device for removal (Win32 error %lu).\n", error);
        return false;
    }

    BOOL needReboot = FALSE;
    const BOOL removed = DiUninstallDevice(nullptr, devices, &deviceInfo, 0, &needReboot);
    const DWORD error = removed ? ERROR_SUCCESS : GetLastError();
    SetupDiDestroyDeviceInfoList(devices);
    if (!removed) {
        if (error == ERROR_ACCESS_DENIED) {
            return remove_vdd_device_with_uac();
        }
        std::fprintf(stderr, "[FastMonitor] DiUninstallDevice failed (Win32 error %lu).\n", error);
        return false;
    }

    g_devInst = 0;
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (find_vdd_devinst() == 0) {
            if (needReboot) {
                std::fprintf(stderr, "[FastMonitor] MttVDD device node removed; Windows also reports that a reboot is needed.\n");
            }
            return true;
        }
        Sleep(200);
    }

    std::fprintf(stderr, "[FastMonitor] Windows accepted MttVDD removal but the device node is still present%s.\n",
            needReboot ? "; a reboot is required" : "");
    return false;
}

} // namespace fastmonitor

// ─────────────────────────────────────────────────────────────────────────────
// JNI helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += L'\"';
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted += ch;
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
}

jstring make_jstring(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

std::string jstring_to_std(JNIEnv* env, jstring js) {
    if (!js) return "";
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result = chars ? chars : "";
    env->ReleaseStringUTFChars(js, chars);
    return result;
}

static std::wstring jstring_to_wide(JNIEnv* env, jstring js) {
    if (!js) return L"";
    const jchar* chars = env->GetStringChars(js, nullptr);
    if (!chars) return L"";
    const jsize length = env->GetStringLength(js);
    std::wstring result(reinterpret_cast<const wchar_t*>(chars), static_cast<size_t>(length));
    env->ReleaseStringChars(js, chars);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// JNI bindings  (generated names must match FastMonitorNative.java)
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_initBackend(JNIEnv*, jclass) {
    return fastmonitor::initBackend() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_fastmonitor_FastMonitorNative_shutdownBackend(JNIEnv*, jclass) {
    fastmonitor::shutdownBackend();
}

JNIEXPORT jint JNICALL Java_fastmonitor_FastMonitorNative_createVirtualMonitor(
        JNIEnv* env, jclass, jint w, jint h, jint hz, jstring name) {
    return (jint)fastmonitor::createVirtualMonitor(
        (int)w, (int)h, (int)hz, jstring_to_std(env, name));
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_destroyVirtualMonitor(
        JNIEnv*, jclass, jint logicalId) {
    return fastmonitor::destroyVirtualMonitor((int)logicalId) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_configureVirtualMonitor(
        JNIEnv*, jclass, jint id, jint w, jint h, jint hz) {
    return fastmonitor::configureVirtualMonitor((int)id, (int)w, (int)h, (int)hz)
           ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_activateVirtualMonitor(
        JNIEnv*, jclass, jint id) {
    return fastmonitor::activateVirtualMonitor((int)id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_deactivateVirtualMonitor(
        JNIEnv*, jclass, jint id) {
    return fastmonitor::deactivateVirtualMonitor((int)id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_fastmonitor_FastMonitorNative_listVirtualMonitorsJson(
        JNIEnv* env, jclass) {
    return make_jstring(env, fastmonitor::listVirtualMonitorsJson());
}

JNIEXPORT jint JNICALL Java_fastmonitor_FastMonitorNative_driverVersion(JNIEnv*, jclass) {
    return (jint)fastmonitor::driverVersion();
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_isDriverPresent(JNIEnv*, jclass) {
    return fastmonitor::isDriverPresent() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_removeDriverDevice(JNIEnv*, jclass) {
    return fastmonitor::removeDriverDevice() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_isEmulationMode(JNIEnv*, jclass) {
    return g_emulationMode.load() ? JNI_TRUE : JNI_FALSE;
}

/** Run the staged driver-only installation command after normal Windows elevation. */
JNIEXPORT jboolean JNICALL Java_fastmonitor_FastMonitorNative_installDriver(
        JNIEnv* env, jclass, jstring commandPath, jstring workingDirectory) {
    const std::wstring command = jstring_to_wide(env, commandPath);
    const std::wstring workingDir = jstring_to_wide(env, workingDirectory);
    if (command.empty() || workingDir.empty()) {
        std::fprintf(stderr, "[FastMonitor] Installer launch failed: an installer path is empty.\n");
        return JNI_FALSE;
    }
    for (const auto& path : { command, workingDir }) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::fprintf(stderr, "[FastMonitor] Installer launch failed: a staged file or directory is missing (Win32 error %lu).\n",
                    GetLastError());
            return JNI_FALSE;
        }
    }

    wchar_t systemDir[MAX_PATH] = {};
    UINT dirLength = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (dirLength == 0 || dirLength >= MAX_PATH) {
        std::fprintf(stderr, "[FastMonitor] Installer launch failed: GetSystemDirectory error %lu.\n", GetLastError());
        return JNI_FALSE;
    }
    const std::wstring commandProcessor = std::wstring(systemDir) + L"\\cmd.exe";
    if (GetFileAttributesW(commandProcessor.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fprintf(stderr, "[FastMonitor] Installer launch failed: cmd.exe not found (Win32 error %lu).\n",
                GetLastError());
        return JNI_FALSE;
    }
    const std::wstring parameters = L"/d /c call " + quote_windows_argument(command);
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = commandProcessor.c_str();
    sei.lpParameters = parameters.c_str();
    sei.lpDirectory  = workingDir.c_str();
    sei.nShow        = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD error = GetLastError();
        std::fprintf(stderr, "[FastMonitor] Could not launch the elevated installer (Win32 error %lu).\n", error);
        return JNI_FALSE;
    }
    if (!sei.hProcess) {
        std::fprintf(stderr, "[FastMonitor] Installer launch returned no process handle.\n");
        return JNI_FALSE;
    }

    const DWORD waitResult = WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool gotExitCode = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    if (!gotExitCode || exitCode != 0) {
        std::fprintf(stderr, "[FastMonitor] Installer process failed: wait=%lu, exit=%lu. Check C:\\ProgramData\\FastMonitor\\FastMonitor-vdd-install.log.\n",
                waitResult, exitCode);
        return JNI_FALSE;
    }

    for (int attempt = 0; attempt < 30 && !is_driver_installed(); ++attempt) {
        Sleep(1000);
    }
    if (!is_driver_installed()) {
        std::fprintf(stderr, "[FastMonitor] Installer exited successfully, but Windows did not report the MttVDD device as present.\n");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

} // extern "C"
