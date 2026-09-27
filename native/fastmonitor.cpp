#include "fastmonitor.h"

#include <setupapi.h>
#include <initguid.h>
#include <mutex>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")

static std::mutex g_mutex;
static std::vector<FastVirtualMonitor> g_monitors;
static bool g_initialized = false;
static HANDLE g_vddHandle = INVALID_HANDLE_VALUE;
static int g_nextLogicalId = 1;

static HANDLE g_keepaliveThread = nullptr;
static bool g_keepaliveRunning = false;

// ---------------------------------------------------------
// Device-Discovery: Parsec VDD via Adapter GUID
// ---------------------------------------------------------

static HANDLE open_vdd_handle()
{
    HDEVINFO devInfo = SetupDiGetClassDevsA(
        &VDD_CLASS_GUID,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (devInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    DWORD index = 0;
    HANDLE result = INVALID_HANDLE_VALUE;

    while (SetupDiEnumDeviceInterfaces(devInfo, nullptr,
                                       &VDD_ADAPTER_GUID,
                                       index, &ifData)) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData,
                                         nullptr, 0,
                                         &requiredSize, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            break;
        }

        auto detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(requiredSize);
        if (!detailData) {
            break;
        }

        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData,
                                              detailData, requiredSize,
                                              nullptr, nullptr)) {
            free(detailData);
            break;
        }

        HANDLE h = CreateFileA(
            detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr
        );

        free(detailData);

        if (h != INVALID_HANDLE_VALUE) {
            result = h;
            break;
        }

        ++index;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

// ---------------------------------------------------------
// Keepalive Thread: VDD_IOCTL_UPDATE
// ---------------------------------------------------------

static DWORD WINAPI keepalive_thread_proc(LPVOID)
{
    while (g_keepaliveRunning) {
        if (g_vddHandle == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }

        BYTE inBuf[0x20] = {};
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            break;
        }

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            g_vddHandle,
            VDD_IOCTL_UPDATE,
            inBuf,
            sizeof(inBuf),
            nullptr,
            0,
            &bytesReturned,
            &ov
        );

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(
                g_vddHandle,
                &ov,
                &bytesReturned,
                TRUE
            );
        }

        CloseHandle(ov.hEvent);
        Sleep(200);
    }
    return 0;
}

// ---------------------------------------------------------
// Backend Implementation
// ---------------------------------------------------------

namespace fastmonitor {

    bool initBackend() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_initialized) {
            return true;
        }

        g_vddHandle = open_vdd_handle();
        // If physical driver is present, use it; otherwise allow software tracking
        g_initialized = true;

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            g_keepaliveRunning = true;
            g_keepaliveThread = CreateThread(
                nullptr,
                0,
                keepalive_thread_proc,
                nullptr,
                0,
                nullptr
            );
        }

        return true;
    }

    void shutdownBackend() {
        std::lock_guard<std::mutex> lock(g_mutex);

        g_keepaliveRunning = false;
        if (g_keepaliveThread) {
            WaitForSingleObject(g_keepaliveThread, 1000);
            CloseHandle(g_keepaliveThread);
            g_keepaliveThread = nullptr;
        }

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_vddHandle);
            g_vddHandle = INVALID_HANDLE_VALUE;
        }

        g_monitors.clear();
        g_nextLogicalId = 1;
        g_initialized = false;
    }

    int createVirtualMonitor(int width,
                             int height,
                             int refreshHz,
                             const std::string& name) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_initialized) {
            return -1;
        }

        int driverIndex = 0;

        if (g_vddHandle != INVALID_HANDLE_VALUE) {
            BYTE inBuf[0x20] = {};
            BYTE outBuf[4]   = {};
            OVERLAPPED ov{};
            ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent) {
                return -1;
            }

            memcpy(&inBuf[0], &width,     sizeof(int));
            memcpy(&inBuf[4], &height,    sizeof(int));
            memcpy(&inBuf[8], &refreshHz, sizeof(int));

            DWORD bytesReturned = 0;
            BOOL ok = DeviceIoControl(
                g_vddHandle,
                VDD_IOCTL_ADD,
                inBuf,
                sizeof(inBuf),
                outBuf,
                sizeof(outBuf),
                &bytesReturned,
                &ov
            );

            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                ok = GetOverlappedResult(
                    g_vddHandle,
                    &ov,
                    &bytesReturned,
                    TRUE
                );
            }

            CloseHandle(ov.hEvent);

            if (!ok || bytesReturned < sizeof(DWORD)) {
                return -1;
            }

            DWORD dIdx = 0;
            memcpy(&dIdx, outBuf, sizeof(DWORD));
            driverIndex = static_cast<int>(dIdx);
        } else {
            // Emulation fallback when driver is not yet installed
            driverIndex = static_cast<int>(g_monitors.size());
        }

        int logicalId = g_nextLogicalId++;
        FastVirtualMonitor vm(logicalId,
                              driverIndex,
                              width,
                              height,
                              refreshHz,
                              name);
        vm.active = true;

        g_monitors.push_back(vm);
        return logicalId;
    }

    bool destroyVirtualMonitor(int logicalId) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_initialized) {
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
            BYTE inBuf[0x20] = {};
            OVERLAPPED ov{};
            ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent) {
                return false;
            }

            DWORD idx = static_cast<DWORD>(it->driverIndex);
            memcpy(&inBuf[1], &idx, sizeof(DWORD));

            DWORD bytesReturned = 0;
            BOOL ok = DeviceIoControl(
                g_vddHandle,
                VDD_IOCTL_REMOVE,
                inBuf,
                sizeof(inBuf),
                nullptr,
                0,
                &bytesReturned,
                &ov
            );

            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                ok = GetOverlappedResult(
                    g_vddHandle,
                    &ov,
                    &bytesReturned,
                    TRUE
                );
            }

            CloseHandle(ov.hEvent);

            if (!ok) {
                return false;
            }
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
                vm.width     = width;
                vm.height    = height;
                vm.refreshHz = refreshHz;
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
        std::string json = "[";
        bool first = true;
        for (const auto& vm : g_monitors) {
            if (!first) json += ",";
            first = false;
            json += "{";
            json += "\"logicalId\":"  + std::to_string(vm.logicalId) + ",";
            json += "\"driverIndex\":" + std::to_string(vm.driverIndex) + ",";
            json += "\"width\":"      + std::to_string(vm.width) + ",";
            json += "\"height\":"     + std::to_string(vm.height) + ",";
            json += "\"refreshHz\":"  + std::to_string(vm.refreshHz) + ",";
            json += "\"name\":\""     + vm.name + "\",";
            json += "\"active\":"     + (vm.active ? std::string("true") : std::string("false"));
            json += "}";
        }
        json += "]";
        return json;
    }

} // namespace fastmonitor

// ---------------------------------------------------------
// JNI helpers & Bindings
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

} // extern "C"
