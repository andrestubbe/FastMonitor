/**
 * vdd_remove.cpp
 * Removes all active VDD (Virtual Display Driver) monitor slots via IOCTL.
 * Use this to clean up monitors created by the VDD control app before running
 * the FastMonitor demo, so the demo starts from a clean state.
 *
 * Compile: cl vdd_remove.cpp /link setupapi.lib
 * Run as Administrator.
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

// VDD GUIDs and IOCTLs (same as fastmonitor.h)
static const GUID VDD_ADAPTER_GUID = {
    0x00b41627, 0x04c4, 0x429e,
    {0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa}
};

#define IOCTL_VDD_REMOVE  0x0022a008

static HANDLE open_vdd_handle() {
    HDEVINFO devInfo = SetupDiGetClassDevs(
        &VDD_ADAPTER_GUID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);

    if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &VDD_ADAPTER_GUID, 0, &ifaceData)) {
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }

    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetail(devInfo, &ifaceData, nullptr, 0, &needed, nullptr);

    auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA*)malloc(needed);
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    SetupDiGetDeviceInterfaceDetail(devInfo, &ifaceData, detail, needed, nullptr, nullptr);
    HANDLE h = CreateFileA(detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    free(detail);
    SetupDiDestroyDeviceInfoList(devInfo);
    return h;
}

int main() {
    printf("VDD Monitor Cleanup Tool\n");
    printf("========================\n\n");

    HANDLE vdd = open_vdd_handle();
    if (vdd == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Could not open VDD handle. Is the driver installed and running?\n");
        printf("        Run as Administrator!\n");
        return 1;
    }
    printf("[OK] VDD handle opened.\n");

    int removed = 0;
    // Try removing slots 0..9
    for (int slot = 0; slot < 10; slot++) {
        DWORD index = (DWORD)slot;
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            vdd, IOCTL_VDD_REMOVE,
            &index, sizeof(index),
            nullptr, 0,
            &bytesReturned, nullptr);
        if (ok) {
            printf("[REMOVED] Slot %d\n", slot);
            removed++;
        }
    }

    CloseHandle(vdd);

    if (removed == 0) {
        printf("\n[INFO] No active VDD monitor slots found. Already clean!\n");
    } else {
        printf("\n[DONE] Removed %d virtual monitor(s).\n", removed);
        printf("       Windows Display Settings should now show only physical monitors.\n");
    }
    return 0;
}
