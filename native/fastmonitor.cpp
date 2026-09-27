#include "fastmonitor.h"

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")

static std::mutex g_mutex;
static std::mutex g_backendOpMutex;
static std::mutex g_ioctlMutex;
static std::vector<FastVirtualMonitor> g_monitors;
static std::atomic<bool> g_initialized{false};
static HANDLE g_vddHandle = INVALID_HANDLE_VALUE;
static int g_nextLogicalId = 1;

static HANDLE g_stopEvent = nullptr;
static HANDLE g_keepaliveThread = nullptr;
static std::atomic<bool> g_keepRunning{false};

// ---------------------------------------------------------
// Synchronous Zero-Allocation IOCTL Dispatcher
// ---------------------------------------------------------

static bool vdd_ioctl(HANDLE h, DWORD code, const void* inData = nullptr, DWORD inSize = 0, void* outData = nullptr, DWORD outSize = 0, DWORD* bytesOut = nullptr) {
    std::lock_guard<std::mutex> lock(g_ioctlMutex);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD returned = 0;
    BOOL ok = DeviceIoControl(
        h,
        code,
        const_cast<void*>(inData),
        inSize,
        outData,
        outSize,
        &returned,
        nullptr
    );

    if (bytesOut) {
        *bytesOut = returned;
    }
    return ok == TRUE;
}

// ---------------------------------------------------------
// Device-Discovery: Parsec VDD via Adapter GUID
// ---------------------------------------------------------

static HANDLE open_vdd_handle() {
    HDEVINFO devInfo = SetupDiGetClassDevsA(
        &VDD_ADAPTER_GUID,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (devInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    HANDLE result = INVALID_HANDLE_VALUE;

    for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &VDD_ADAPTER_GUID, idx, &ifData); ++idx) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            break;
        }

        auto detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(malloc(requiredSize));
        if (!detailData) {
            break;
        }

        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, detailData, requiredSize, nullptr, nullptr)) {
            HANDLE h = CreateFileA(
                detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            free(detailData);
            if (h != INVALID_HANDLE_VALUE) {
                result = h;
                break;
            }
        } else {
            free(detailData);
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

// ---------------------------------------------------------
// Keepalive Watchdog Thread (100 ms interval)
// ---------------------------------------------------------

static DWORD WINAPI keepalive_thread_proc(LPVOID) {
    while (g_keepRunning.load(std::memory_order_relaxed)) {
        DWORD wait = WaitForSingleObject(g_stopEvent, 100);
        if (wait == WAIT_OBJECT_0) {
            break;
        }

        HANDLE device = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            device = g_vddHandle;
        }

        if (device != INVALID_HANDLE_VALUE) {
            vdd_ioctl(device, VDD_IOCTL_UPDATE);
        }
    }
    return 0;
}

// ---------------------------------------------------------
// Win32 Display Configuration Helper (outside global lock)
// ---------------------------------------------------------

static std::string find_vdd_display_device_for_slot(int driverIndex) {
    DISPLAY_DEVICEA dd{};
    dd.cb = sizeof(dd);

    std::vector<std::string> vddDevices;
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            continue;
        }
        if (strstr(dd.DeviceString, "Parsec") || strstr(dd.DeviceID, "Parsec") ||
            strstr(dd.DeviceID, "VDA") || strstr(dd.DeviceID, "PSCCDD")) {
            vddDevices.emplace_back(dd.DeviceName);
        }
    }

    if (driverIndex >= 0 && static_cast<size_t>(driverIndex) < vddDevices.size()) {
        return vddDevices[driverIndex];
    }
    if (!vddDevices.empty()) {
        return vddDevices.back();
    }
    return "";
}

static bool apply_display_mode(const std::string& deviceName, int width, int height, int refreshHz) {
    if (deviceName.empty()) {
        return false;
    }

    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsA(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return false;
    }

    if (dm.dmPelsWidth == static_cast<DWORD>(width) &&
        dm.dmPelsHeight == static_cast<DWORD>(height) &&
        dm.dmDisplayFrequency == static_cast<DWORD>(refreshHz)) {
        return true;
    }

    dm.dmPelsWidth = static_cast<DWORD>(width);
    dm.dmPelsHeight = static_cast<DWORD>(height);
    dm.dmDisplayFrequency = static_cast<DWORD>(refreshHz);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    LONG res = ChangeDisplaySettingsExA(deviceName.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    if (res == DISP_CHANGE_SUCCESSFUL) {
        ChangeDisplaySettingsExA(nullptr, nullptr, nullptr, 0, nullptr);
        return true;
    }
    return false;
}

static void append_json_escaped(std::string& out, const std::string& str) {
    for (unsigned char c : str) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
}

// ---------------------------------------------------------
// Backend Implementation
// ---------------------------------------------------------

namespace fastmonitor {

    bool initBackend() {
        std::lock_guard<std::mutex> opLock(g_backendOpMutex);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_initialized.load(std::memory_order_acquire)) {
            return true;
        }

        g_vddHandle = open_vdd_handle();
        g_monitors.reserve(VDD_MAX_DISPLAYS);

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            g_stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!g_stopEvent) {
                CloseHandle(g_vddHandle);
                g_vddHandle = INVALID_HANDLE_VALUE;
                return false;
            }
            g_keepRunning.store(true, std::memory_order_release);
            g_keepaliveThread = CreateThread(nullptr, 0, keepalive_thread_proc, nullptr, 0, nullptr);
            if (!g_keepaliveThread) {
                g_keepRunning.store(false, std::memory_order_release);
                CloseHandle(g_stopEvent);
                CloseHandle(g_vddHandle);
                g_stopEvent = nullptr;
                g_vddHandle = INVALID_HANDLE_VALUE;
                return false;
            }
        }

        g_initialized.store(true, std::memory_order_release);
        return true;
    }

    void shutdownBackend() {
        std::lock_guard<std::mutex> opLock(g_backendOpMutex);
        HANDLE thread = nullptr;
        HANDLE device = INVALID_HANDLE_VALUE;
        HANDLE stopEv = nullptr;
        std::vector<int> driverIndices;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_initialized.load(std::memory_order_acquire)) {
                return;
            }

            g_keepRunning.store(false, std::memory_order_release);
            stopEv = g_stopEvent;
            thread = g_keepaliveThread;
            device = g_vddHandle;

            for (const auto& vm : g_monitors) {
                if (vm.driverIndex >= 0) {
                    driverIndices.push_back(vm.driverIndex);
                }
            }

            g_stopEvent = nullptr;
            g_keepaliveThread = nullptr;
            g_vddHandle = INVALID_HANDLE_VALUE;
            g_monitors.clear();
            g_nextLogicalId = 1;
            g_initialized.store(false, std::memory_order_release);
        }

        if (stopEv) {
            SetEvent(stopEv);
        }

        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }

        if (stopEv) {
            CloseHandle(stopEv);
        }

        if (device != INVALID_HANDLE_VALUE) {
            for (int dIdx : driverIndices) {
                UINT16 beIdx = static_cast<UINT16>(
                    ((dIdx & 0xFF) << 8) | ((dIdx >> 8) & 0xFF)
                );
                vdd_ioctl(device, VDD_IOCTL_REMOVE, &beIdx, sizeof(beIdx));
            }
            vdd_ioctl(device, VDD_IOCTL_UPDATE);
            CloseHandle(device);
        }
    }

    int createVirtualMonitor(int width,
                             int height,
                             int refreshHz,
                             const std::string& name) {
        std::lock_guard<std::mutex> opLock(g_backendOpMutex);
        int logicalId = -1;
        int driverIndex = -1;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_initialized.load(std::memory_order_acquire)) {
                return -1;
            }

            if (g_monitors.size() >= VDD_MAX_DISPLAYS) {
                return -1;
            }

            if (g_vddHandle != INVALID_HANDLE_VALUE) {
                DWORD dIdx = 0;
                DWORD bytesReturned = 0;
                if (!vdd_ioctl(g_vddHandle, VDD_IOCTL_ADD, nullptr, 0, &dIdx, sizeof(dIdx), &bytesReturned) ||
                    bytesReturned < sizeof(dIdx) || dIdx >= VDD_MAX_DISPLAYS) {
                    return -1;
                }
                driverIndex = static_cast<int>(dIdx);
                vdd_ioctl(g_vddHandle, VDD_IOCTL_UPDATE);
            } else {
                // Emulation mode: allocate lowest unused index [0..7]
                bool used[VDD_MAX_DISPLAYS] = {false};
                for (const auto& m : g_monitors) {
                    if (m.driverIndex >= 0 && static_cast<size_t>(m.driverIndex) < VDD_MAX_DISPLAYS) {
                        used[m.driverIndex] = true;
                    }
                }
                for (size_t i = 0; i < VDD_MAX_DISPLAYS; ++i) {
                    if (!used[i]) {
                        driverIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (driverIndex < 0) {
                    return -1;
                }
            }

            logicalId = g_nextLogicalId++;
            g_monitors.emplace_back(logicalId, driverIndex, width, height, refreshHz, name);
        }

        // Apply display mode to OS display device if hardware driver is active
        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            std::string devName = find_vdd_display_device_for_slot(driverIndex);
            if (!apply_display_mode(devName, width, height, refreshHz)) {
                // Rollback driver slot allocation on failure
                UINT16 beIdx = static_cast<UINT16>(
                    ((driverIndex & 0xFF) << 8) | ((driverIndex >> 8) & 0xFF)
                );
                vdd_ioctl(g_vddHandle, VDD_IOCTL_REMOVE, &beIdx, sizeof(beIdx));
                vdd_ioctl(g_vddHandle, VDD_IOCTL_UPDATE);

                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = std::find_if(
                    g_monitors.begin(),
                    g_monitors.end(),
                    [logicalId](const FastVirtualMonitor& vm) {
                        return vm.logicalId == logicalId;
                    }
                );
                if (it != g_monitors.end()) {
                    g_monitors.erase(it);
                }
                return -1;
            }

            // Persist detected OS deviceName
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                for (auto& vm : g_monitors) {
                    if (vm.logicalId == logicalId) {
                        vm.deviceName = devName;
                        break;
                    }
                }
            }
        }

        return logicalId;
    }

    bool destroyVirtualMonitor(int logicalId) {
        std::lock_guard<std::mutex> opLock(g_backendOpMutex);
        int driverIndex = -1;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_initialized.load(std::memory_order_acquire)) {
                return false;
            }

            auto it = std::find_if(
                g_monitors.begin(),
                g_monitors.end(),
                [logicalId](const FastVirtualMonitor& vm) {
                    return vm.logicalId == logicalId;
                }
            );

            if (it == g_monitors.end()) {
                return false;
            }

            driverIndex = it->driverIndex;
        }

        if (driverIndex >= 0) {
            HANDLE device = INVALID_HANDLE_VALUE;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                device = g_vddHandle;
            }
            if (device != INVALID_HANDLE_VALUE) {
                UINT16 beIdx = static_cast<UINT16>(
                    ((driverIndex & 0xFF) << 8) | ((driverIndex >> 8) & 0xFF)
                );
                bool removed = vdd_ioctl(device, VDD_IOCTL_REMOVE, &beIdx, sizeof(beIdx));
                if (!removed) {
                    return false;
                }
                vdd_ioctl(device, VDD_IOCTL_UPDATE);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = std::find_if(
                g_monitors.begin(),
                g_monitors.end(),
                [logicalId](const FastVirtualMonitor& vm) {
                    return vm.logicalId == logicalId;
                }
            );
            if (it != g_monitors.end()) {
                g_monitors.erase(it);
            }
        }

        return true;
    }

    bool configureVirtualMonitor(int logicalId,
                                 int width,
                                 int height,
                                 int refreshHz) {
        std::lock_guard<std::mutex> opLock(g_backendOpMutex);
        int driverIndex = -1;
        std::string deviceName;
        bool hasHardwareDriver = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            hasHardwareDriver = (g_vddHandle != INVALID_HANDLE_VALUE);
            for (const auto& vm : g_monitors) {
                if (vm.logicalId == logicalId) {
                    driverIndex = vm.driverIndex;
                    deviceName = vm.deviceName;
                    break;
                }
            }
        }

        if (driverIndex < 0) {
            return false;
        }

        if (deviceName.empty()) {
            deviceName = find_vdd_display_device_for_slot(driverIndex);
        }

        // Apply display mode to OS display device if hardware driver is active
        if (hasHardwareDriver && !apply_display_mode(deviceName, width, height, refreshHz)) {
            return false;
        }

        // Commit updated state
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& vm : g_monitors) {
                if (vm.logicalId == logicalId) {
                    vm.width = width;
                    vm.height = height;
                    vm.refreshHz = refreshHz;
                    vm.deviceName = deviceName;
                    break;
                }
            }
        }

        return true;
    }

    bool activateVirtualMonitor(int logicalId) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& vm : g_monitors) {
            if (vm.logicalId == logicalId) {
                vm.active = true;
                return true;
            }
        }
        return false;
    }

    bool deactivateVirtualMonitor(int logicalId) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& vm : g_monitors) {
            if (vm.logicalId == logicalId) {
                vm.active = false;
                return true;
            }
        }
        return false;
    }

    std::string listVirtualMonitorsJson() {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string json;
        json.reserve(1024);
        json += "[";

        bool first = true;
        for (const auto& vm : g_monitors) {
            if (!first) {
                json += ",";
            }
            first = false;
            json += "{\"logicalId\":";
            json += std::to_string(vm.logicalId);
            json += ",\"driverIndex\":";
            json += std::to_string(vm.driverIndex);
            json += ",\"width\":";
            json += std::to_string(vm.width);
            json += ",\"height\":";
            json += std::to_string(vm.height);
            json += ",\"refreshHz\":";
            json += std::to_string(vm.refreshHz);
            json += ",\"name\":\"";
            append_json_escaped(json, vm.name);
            json += "\",\"active\":";
            json += vm.active ? "true" : "false";
            json += "}";
        }
        json += "]";
        return json;
    }

    int driverVersion() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_vddHandle == INVALID_HANDLE_VALUE) {
            return 0;
        }
        DWORD ver = 0;
        if (!vdd_ioctl(g_vddHandle, VDD_IOCTL_VERSION, nullptr, 0, &ver, sizeof(ver), nullptr)) {
            return 0;
        }
        return static_cast<int>(ver);
    }

    bool isDriverPresent() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_vddHandle != INVALID_HANDLE_VALUE;
    }

} // namespace fastmonitor

// ---------------------------------------------------------
// JNI Helpers & Bindings
// ---------------------------------------------------------

jstring make_jstring(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

std::string jstring_to_std(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* utf = env->GetStringUTFChars(js, nullptr);
    std::string out(utf ? utf : "");
    env->ReleaseStringUTFChars(js, utf);
    return out;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_initBackend(JNIEnv* env, jclass) {
    (void)env;
    return fastmonitor::initBackend() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_fastmonitor_FastMonitorNative_shutdownBackend(JNIEnv* env, jclass) {
    (void)env;
    fastmonitor::shutdownBackend();
}

JNIEXPORT jint JNICALL
Java_fastmonitor_FastMonitorNative_createVirtualMonitor(JNIEnv* env,
                                                        jclass,
                                                        jint width,
                                                        jint height,
                                                        jint refreshHz,
                                                        jstring jname) {
    std::string name = jstring_to_std(env, jname);
    return fastmonitor::createVirtualMonitor(width, height, refreshHz, name);
}

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_destroyVirtualMonitor(JNIEnv* env,
                                                         jclass,
                                                         jint logicalId) {
    (void)env;
    return fastmonitor::destroyVirtualMonitor(logicalId) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_configureVirtualMonitor(JNIEnv* env,
                                                           jclass,
                                                           jint logicalId,
                                                           jint width,
                                                           jint height,
                                                           jint refreshHz) {
    (void)env;
    return fastmonitor::configureVirtualMonitor(logicalId, width, height, refreshHz)
           ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_fastmonitor_FastMonitorNative_listVirtualMonitors(JNIEnv* env,
                                                       jclass) {
    std::string json = fastmonitor::listVirtualMonitorsJson();
    return make_jstring(env, json);
}

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_activateVirtualMonitor(JNIEnv* env,
                                                          jclass,
                                                          jint logicalId) {
    (void)env;
    return fastmonitor::activateVirtualMonitor(logicalId) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_deactivateVirtualMonitor(JNIEnv* env,
                                                            jclass,
                                                            jint logicalId) {
    (void)env;
    return fastmonitor::deactivateVirtualMonitor(logicalId) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_fastmonitor_FastMonitorNative_isDriverPresent(JNIEnv* env, jclass) {
    (void)env;
    return fastmonitor::isDriverPresent() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_fastmonitor_FastMonitorNative_driverVersion(JNIEnv* env, jclass) {
    (void)env;
    return fastmonitor::driverVersion();
}

} // extern "C"
