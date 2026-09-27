#include "fastmonitor.h"

#include <setupapi.h>
#include <initguid.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "user32.lib")

static std::mutex g_mutex;
static std::vector<FastVirtualMonitor> g_monitors;
static std::atomic<bool> g_initialized{false};
static HANDLE g_vddHandle = INVALID_HANDLE_VALUE;
static int g_nextLogicalId = 1;

static HANDLE g_stopEvent = nullptr;
static HANDLE g_keepaliveThread = nullptr;
static std::atomic<bool> g_keepRunning{false};

// ---------------------------------------------------------
// Fast Stack-Based IOCTL Dispatcher
// ---------------------------------------------------------

static DWORD vdd_ioctl(HANDLE h, DWORD code, const void* inData = nullptr, size_t inSize = 0) {
    if (h == INVALID_HANDLE_VALUE) {
        return static_cast<DWORD>(-1);
    }

    BYTE inBuf[32]{};
    if (inData && inSize > 0) {
        memcpy(inBuf, inData, (inSize < 32) ? inSize : 32);
    }

    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return static_cast<DWORD>(-1);
    }

    DWORD outVal = 0;
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        h,
        code,
        inBuf,
        sizeof(inBuf),
        &outVal,
        sizeof(outVal),
        &bytesReturned,
        &ov
    );

    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        ok = GetOverlappedResult(h, &ov, &bytesReturned, TRUE);
    }

    CloseHandle(ov.hEvent);
    return ok ? outVal : static_cast<DWORD>(-1);
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
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH,
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
// Win32 Display Configuration Helper (ChangeDisplaySettingsEx)
// ---------------------------------------------------------

static bool apply_display_mode(int driverIndex, int width, int height, int refreshHz) {
    DISPLAY_DEVICEA dd{};
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {
        if (strstr(dd.DeviceString, "Parsec") || strstr(dd.DeviceID, "Parsec") || strstr(dd.DeviceID, "VDA")) {
            DEVMODEA dm{};
            dm.dmSize = sizeof(dm);
            dm.dmPelsWidth = static_cast<DWORD>(width);
            dm.dmPelsHeight = static_cast<DWORD>(height);
            dm.dmDisplayFrequency = static_cast<DWORD>(refreshHz);
            dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

            LONG res = ChangeDisplaySettingsExA(dd.DeviceName, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
            if (res == DISP_CHANGE_SUCCESSFUL) {
                ChangeDisplaySettingsExA(dd.DeviceName, nullptr, nullptr, 0, nullptr);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------
// Backend Implementation
// ---------------------------------------------------------

namespace fastmonitor {

    bool initBackend() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_initialized.load(std::memory_order_acquire)) {
            return true;
        }

        g_vddHandle = open_vdd_handle();
        g_monitors.reserve(8); // Parsec VDD supports up to 8 displays

        g_stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (g_vddHandle != INVALID_HANDLE_VALUE && g_stopEvent != nullptr) {
            g_keepRunning.store(true, std::memory_order_release);
            g_keepaliveThread = CreateThread(nullptr, 0, keepalive_thread_proc, nullptr, 0, nullptr);
        }

        g_initialized.store(true, std::memory_order_release);
        return true;
    }

    void shutdownBackend() {
        HANDLE thread = nullptr;
        HANDLE device = INVALID_HANDLE_VALUE;
        HANDLE stopEv = nullptr;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_initialized.load(std::memory_order_acquire)) {
                return;
            }

            g_keepRunning.store(false, std::memory_order_release);
            stopEv = g_stopEvent;
            thread = g_keepaliveThread;
            device = g_vddHandle;

            g_stopEvent = nullptr;
            g_keepaliveThread = nullptr;
            g_vddHandle = INVALID_HANDLE_VALUE;
            g_initialized.store(false, std::memory_order_release);
        }

        if (stopEv) {
            SetEvent(stopEv);
        }

        if (device != INVALID_HANDLE_VALUE) {
            CancelIoEx(device, nullptr);
        }

        if (thread) {
            WaitForSingleObject(thread, 1000);
            CloseHandle(thread);
        }

        if (stopEv) {
            CloseHandle(stopEv);
        }

        if (device != INVALID_HANDLE_VALUE) {
            CloseHandle(device);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        g_monitors.clear();
        g_nextLogicalId = 1;
    }

    int createVirtualMonitor(int width,
                             int height,
                             int refreshHz,
                             const std::string& name) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_initialized.load(std::memory_order_acquire)) {
            return -1;
        }

        int driverIndex = -1;

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            // Official Parsec VDD protocol: ADD takes NO payload, returns driver slot index
            DWORD idx = vdd_ioctl(g_vddHandle, VDD_IOCTL_ADD);
            if (idx == static_cast<DWORD>(-1)) {
                return -1;
            }
            driverIndex = static_cast<int>(idx);
            vdd_ioctl(g_vddHandle, VDD_IOCTL_UPDATE); // Immediate ping to prevent timeout
        } else {
            // Emulation mode for testing when driver is absent
            driverIndex = static_cast<int>(g_monitors.size());
        }

        int logicalId = g_nextLogicalId++;
        g_monitors.emplace_back(logicalId, driverIndex, width, height, refreshHz, name);

        // Apply Win32 display mode settings
        apply_display_mode(driverIndex, width, height, refreshHz);

        return logicalId;
    }

    bool destroyVirtualMonitor(int logicalId) {
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

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            // Official protocol: 16-bit big-endian driver index
            UINT16 beIdx = static_cast<UINT16>(
                ((it->driverIndex & 0xFF) << 8) | ((it->driverIndex >> 8) & 0xFF)
            );
            vdd_ioctl(g_vddHandle, VDD_IOCTL_REMOVE, &beIdx, sizeof(beIdx));
            vdd_ioctl(g_vddHandle, VDD_IOCTL_UPDATE);
        }

        g_monitors.erase(it);
        return true;
    }

    bool configureVirtualMonitor(int logicalId,
                                 int width,
                                 int height,
                                 int refreshHz) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& vm : g_monitors) {
            if (vm.logicalId == logicalId) {
                vm.width = width;
                vm.height = height;
                vm.refreshHz = refreshHz;
                apply_display_mode(vm.driverIndex, width, height, refreshHz);
                return true;
            }
        }
        return false;
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

            // JSON escape name
            for (char c : vm.name) {
                if (c == '"' || c == '\\') {
                    json += '\\';
                }
                json += c;
            }

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
        DWORD ver = vdd_ioctl(g_vddHandle, VDD_IOCTL_VERSION);
        return (ver != static_cast<DWORD>(-1)) ? static_cast<int>(ver) : 0;
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
