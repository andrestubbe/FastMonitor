#pragma once

#include <jni.h>
#include <string>
#include <vector>
#include <windows.h>

// Official Parsec VDD Constants & Protocol
// Class GUID:   {4d36e968-e325-11ce-bfc1-08002be10318}
// Adapter GUID: {00b41627-04c4-429e-a26e-0265cf50c8fa}
static const GUID VDD_CLASS_GUID =
{ 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

static const GUID VDD_ADAPTER_GUID =
{ 0x00b41627, 0x04c4, 0x429e, { 0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa } };

enum : DWORD {
    VDD_IOCTL_ADD     = 0x0022e004,
    VDD_IOCTL_REMOVE  = 0x0022a008,
    VDD_IOCTL_UPDATE  = 0x0022a00c,
    VDD_IOCTL_VERSION = 0x0022e010
};

struct FastVirtualMonitor {
    int logicalId;
    int driverIndex;   // Driver slot returned by VDD_IOCTL_ADD
    int width;
    int height;
    int refreshHz;
    std::string name;
    bool active;

    FastVirtualMonitor(int logicalId_,
                       int driverIndex_,
                       int w,
                       int h,
                       int hz,
                       std::string n)
        : logicalId(logicalId_),
          driverIndex(driverIndex_),
          width(w),
          height(h),
          refreshHz(hz),
          name(std::move(n)),
          active(true) {}
};

namespace fastmonitor {
    bool initBackend();
    void shutdownBackend();

    int createVirtualMonitor(int width,
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
    int driverVersion();
    bool isDriverPresent();
}

jstring make_jstring(JNIEnv* env, const std::string& s);
std::string jstring_to_std(JNIEnv* env, jstring js);
