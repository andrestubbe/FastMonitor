#pragma once

#include <jni.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// ─── MikeTheTech VDD (VirtualDrivers) constants ──────────────────────────────
// Hardware ID used to locate the device in the PnP tree
static constexpr const char* VDD_HARDWARE_ID       = "Root\\MttVDD";
static constexpr const char* VDD_INSTANCE_ID        = "ROOT\\DISPLAY\\0000";

// Default path written by the VDD installer
static constexpr const char* VDD_INSTALL_DIR        = "C:\\VirtualDisplayDriver";
static constexpr const char* VDD_SETTINGS_XML       = "C:\\VirtualDisplayDriver\\vdd_settings.xml";

// Maximum number of virtual monitors the driver supports
static constexpr int VDD_MAX_MONITORS = 4;

// Milliseconds to wait after disable/enable for device to settle
static constexpr DWORD VDD_RESTART_DISABLE_MS = 600;
static constexpr DWORD VDD_RESTART_ENABLE_MS  = 2000;

// ─── Monitor struct ───────────────────────────────────────────────────────────
struct FastVirtualMonitor {
    int         logicalId;
    int         slotIndex;      // 0-based index into driver's monitor list
    int         width;
    int         height;
    int         refreshHz;
    std::string name;
    std::string deviceName;     // e.g. "\\.\DISPLAY2" — filled after device restart
    bool        active;

    FastVirtualMonitor(int logicalId_,
                       int slotIndex_,
                       int w,
                       int h,
                       int hz,
                       std::string  n,
                       std::string  devName = "")
        : logicalId(logicalId_),
          slotIndex(slotIndex_),
          width(w),
          height(h),
          refreshHz(hz),
          name(std::move(n)),
          deviceName(std::move(devName)),
          active(true) {}
};

// ─── Public API ───────────────────────────────────────────────────────────────
namespace fastmonitor {
    bool initBackend();
    void shutdownBackend();

    int  createVirtualMonitor(int width,
                              int height,
                              int refreshHz,
                              const std::string& name);

    bool destroyVirtualMonitor(int logicalId);
    bool configureVirtualMonitor(int logicalId,
                                 int width,
                                 int height,
                                 int refreshHz);

    bool activateVirtualMonitor(int logicalId);
    bool deactivateVirtualMonitor(int logicalId);

    std::string listVirtualMonitorsJson();
    int  driverVersion();
    bool isDriverPresent();
    bool removeDriverDevice();
}

// ─── JNI helpers ─────────────────────────────────────────────────────────────
jstring     make_jstring(JNIEnv* env, const std::string& s);
std::string jstring_to_std(JNIEnv* env, jstring js);
