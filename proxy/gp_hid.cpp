// hid
#include "gp_hid.h"
#include "gp_log.h"

#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace gphid {
namespace {

const int kMaxDevices = 16;
Device    g_devices[kMaxDevices];
int       g_count     = 0;
CRITICAL_SECTION g_lock;
volatile LONG    g_lockReady = 0;













DWORD g_vendorIds[8] = { 0x045E };
int   g_vendorIdCount = 1;
BOOL  g_anyGamepad = FALSE;








BOOL  g_allowBluetooth = FALSE;







BOOL IsXboxPadPid(USHORT pid) {
    switch (pid) {
    case 0x028E: case 0x028F: case 0x0719:            
    case 0x02D1: case 0x02DD: case 0x02E0:            
    case 0x02EA: case 0x02FD:
    case 0x02E3:                                      
    case 0x0B00: case 0x0B05:                         
    case 0x0B0A: case 0x0B12: case 0x0B13:            
        return TRUE;
    default:
        return FALSE;                                 
    }
}

BOOL VendorAllowed(USHORT vid) {
    if (g_anyGamepad) return TRUE;
    for (int i = 0; i < g_vendorIdCount; ++i) {
        if (g_vendorIds[i] == vid) return TRUE;
    }
    return FALSE;
}

void EnsureLock(void) {
    if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
    }
}



BOOL IsBluetoothInstance(const wchar_t* instanceId, const wchar_t* path) {
    if (instanceId && wcsstr(instanceId, L"BTHENUM")) return TRUE;
    if (instanceId) {
        
        if (wcsstr(instanceId, L"BTHLEDEVICE")) return TRUE;
    }
    if (path && wcsstr(path, L"BTHENUM")) return TRUE;
    return FALSE;
}

void CloseDevice(Device* d) {
    if (d->hDevice && d->hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(d->hDevice);
    }
    d->hDevice = INVALID_HANDLE_VALUE;
    d->usable = FALSE;
}



BOOL ProbeReportId(HANDLE h, BYTE reportId) {
    BYTE report[9] = {0};
    report[0] = reportId;   
    report[1] = 0x0F;       
    report[6] = 255;        
    report[7] = 0;          
    report[8] = 255;        

    DWORD written = 0;
    return WriteFile(h, report, sizeof(report), &written, nullptr);
}





int ProbeIoctl(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return 0;
    BYTE payload[7] = {0};
    DWORD ret = 0;
    if (DeviceIoControl(h, 0x002aac08, payload, 7, nullptr, 0, &ret, nullptr)) return 1;
    if (DeviceIoControl(h, 0x8000a010, payload, 4, nullptr, 0, &ret, nullptr)) return 2;
    return 0;
}

BOOL OpenOne(const wchar_t* path, Device* out) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        
        return FALSE;
    }

    HIDD_ATTRIBUTES attrs = {0};
    attrs.Size = sizeof(attrs);
    if (!HidD_GetAttributes(h, &attrs)) {
        CloseHandle(h);
        return FALSE;
    }

    

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps = {0};
    BOOL haveCaps = FALSE;
    if (HidD_GetPreparsedData(h, &preparsed)) {
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            haveCaps = TRUE;
        }
        HidD_FreePreparsedData(preparsed);
    }
    


    BOOL reportTooShort = (haveCaps && caps.OutputReportByteLength < 9);
    if (reportTooShort) {
        HIDD_ATTRIBUTES a = {0};
        a.Size = sizeof(a);
        if (HidD_GetAttributes(h, &a) && g_allowBluetooth) {
            
            GP_LOG_DEBUG("gphid: 跳过 VID=%04X PID=%04X —— 输出报告只有 %u 字节",
                         a.VendorID, a.ProductID, caps.OutputReportByteLength);
            CloseHandle(h);
            return FALSE;
        }
    }
    if (FALSE && haveCaps && caps.OutputReportByteLength < 9) {
        



        HIDD_ATTRIBUTES a = {0};
        a.Size = sizeof(a);
        if (HidD_GetAttributes(h, &a)) {
            GP_LOG_DEBUG("gphid: 跳过 VID=%04X PID=%04X —— 输出报告只有 %u 字节，"
                         "放不下 9 字节的震动报告", a.VendorID, a.ProductID,
                         caps.OutputReportByteLength);
        }
        CloseHandle(h);
        return FALSE;
    }

    BOOL gamepadUsage = haveCaps && caps.UsagePage == 0x01 &&
                        (caps.Usage == 0x05 || caps.Usage == 0x04);

    

    


    BOOL xboxPad = (attrs.VendorID == 0x045E) && IsXboxPadPid(attrs.ProductID);

    if (!VendorAllowed(attrs.VendorID)) {
        if (!(g_anyGamepad && gamepadUsage)) {
            CloseHandle(h);
            return FALSE;
        }
    }

    wcsncpy_s(out->path, 512, path, _TRUNCATE);
    out->vid  = attrs.VendorID;
    out->gamepadUsage = gamepadUsage;
    out->trustedFormat = (attrs.VendorID == 0x045E);
    out->pid  = attrs.ProductID;
    out->outputReportBytes = haveCaps ? caps.OutputReportByteLength : 0;
    out->bluetooth = IsBluetoothInstance(out->instanceId, path);

    if (out->bluetooth && !g_allowBluetooth) {
        GP_LOG_INFO("gphid: 跳过蓝牙手柄 VID=%04X PID=%04X —— 蓝牙的输出报告"
                    "布局与有线不同，写错会让电机乱震。已改用 "
                    "Windows.Gaming.Input/XInput 通道（格式由驱动负责）。"
                    "确要用蓝牙 HID 就在 ini 里设 HidAllowBluetooth=true",
                    attrs.VendorID, attrs.ProductID);
        CloseHandle(h);
        return FALSE;
    }

    out->hDevice = h;

    

    BYTE first  = out->bluetooth ? 0x03 : 0x00;
    BYTE second = out->bluetooth ? 0x00 : 0x03;

    if (!reportTooShort && ProbeReportId(h, first)) {
        out->reportId = first;
        out->usable = TRUE;
    } else if (!reportTooShort && ProbeReportId(h, second)) {
        out->reportId = second;
        out->usable = TRUE;
        GP_LOG_DEBUG("gphid: 报告 ID 与连接方式不符，实测可用的是 0x%02X", second);
    } else if (!reportTooShort) {
        out->reportId = first;
        out->usable = FALSE;
        GP_LOG_INFO("gphid: 找到了手柄接口但写不进去（多半是没有管理员权限）: %s",
                    gplog::W(path));
    } else {
        out->reportId = 0;
        out->usable = FALSE;
    }

    


    


    if (xboxPad && (h == INVALID_HANDLE_VALUE || !h)) {
        h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
    }

    out->ioctlKind = ProbeIoctl(h);

    if (out->ioctlKind == 0 && !out->usable) {
        

        if (xboxPad) {
            GP_LOG_INFO("gphid: 接口 VID=%04X PID=%04X 两条路都不通（多半是只读的"
                        "DInput 兼容节点），跳过", attrs.VendorID, attrs.ProductID);
        }
        CloseHandle(h);
        return FALSE;
    }
    if (out->ioctlKind != 0) {
        DWORD zero7[7] = {0};
        (void)zero7;
        GP_LOG_INFO("gphid:   IOCTL 通道可用：%s（%s）",
                    out->ioctlKind == 1 ? "Steam 7 字节（四电机）" : "微软 4 字节（只有体感）",
                    gplog::W(out->bluetooth ? L"蓝牙" : L"有线/2.4G"));
    }

    return TRUE;
}

void EnumerateInto(Device* list, int* count, int maxCount) {
    *count = 0;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        GP_LOG_ERROR("gphid: SetupDiGetClassDevs 失败 (err=%lu)", GetLastError());
        return;
    }

    SP_DEVICE_INTERFACE_DATA iface = {0};
    iface.cbSize = sizeof(iface);

    for (DWORD i = 0; i < 256 && *count < maxCount; ++i) {
        if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &iface)) break;

        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        

        BYTE* buf = (BYTE*)_alloca(needed);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devData = {0};
        devData.cbSize = sizeof(devData);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &iface, detail, needed,
                                              nullptr, &devData)) {
            continue;
        }

        Device d;
        memset(&d, 0, sizeof(d));
        d.hDevice = INVALID_HANDLE_VALUE;

        

        {
            static int sSeen = 0;
            if (sSeen < 24) {
                HANDLE probe = CreateFileW(detail->DevicePath, 0,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr, OPEN_EXISTING, 0, nullptr);
                if (probe != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES a = {0};
                    a.Size = sizeof(a);
                    HIDP_CAPS c = {0};
                    PHIDP_PREPARSED_DATA pp = nullptr;
                    BOOL caps = FALSE;
                    if (HidD_GetPreparsedData(probe, &pp)) {
                        caps = (HidP_GetCaps(pp, &c) == HIDP_STATUS_SUCCESS);
                        HidD_FreePreparsedData(pp);
                    }
                    if (HidD_GetAttributes(probe, &a)) {
                        ++sSeen;
                        GP_LOG_INFO("gphid: 接口[%d] VID=%04X PID=%04X%s 用途=%04X:%04X 输出报告=%u 字节",
                                    sSeen, a.VendorID, a.ProductID,
                                    IsXboxPadPid(a.ProductID) ? "(Xbox手柄)" : "",
                                    caps ? c.UsagePage : 0, caps ? c.Usage : 0,
                                    caps ? c.OutputReportByteLength : 0);
                    }
                    CloseHandle(probe);
                }
            }
        }

        
        SetupDiGetDeviceInstanceIdW(devInfo, &devData, d.instanceId, 512, nullptr);

        if (OpenOne(detail->DevicePath, &d)) {
            list[(*count)++] = d;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

}  

void SetBluetoothAllowed(BOOL on) { g_allowBluetooth = on; }






BOOL IsOurHandle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    for (int i = 0; i < g_count && i < kMaxDevices; ++i) {
        if (g_devices[i].hDevice == h) return TRUE;
    }
    return FALSE;
}

void SetVendorFilter(const DWORD* vendorIds, int count, BOOL anyGamepad) {
    EnsureLock();
    EnterCriticalSection(&g_lock);
    g_anyGamepad = anyGamepad;
    if (vendorIds && count > 0) {
        g_vendorIdCount = count > 8 ? 8 : count;
        for (int i = 0; i < g_vendorIdCount; ++i) g_vendorIds[i] = vendorIds[i];
    }
    LeaveCriticalSection(&g_lock);
}

int Init(void) {
    EnsureLock();
    EnterCriticalSection(&g_lock);

    for (int i = 0; i < g_count; ++i) CloseDevice(&g_devices[i]);
    g_count = 0;

    EnumerateInto(g_devices, &g_count, kMaxDevices);

    int usable = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_devices[i].usable) ++usable;
    }
    GP_LOG_INFO("gphid: 枚举到 %d 个可用的 HID 输出接口，其中 %d 个可写",
                g_count, usable);
    for (int i = 0; i < g_count; ++i) {
        const Device& d = g_devices[i];
        GP_LOG_INFO("gphid:   [%d] VID=%04X PID=%04X %s ReportID=0x%02X 可写=%d %s %s",
                    i, d.vid, d.pid, gplog::W(d.bluetooth ? L"蓝牙" : L"有线/2.4G"),
                    d.reportId, d.usable ? 1 : 0,
                    d.trustedFormat ? L"报告格式:微软标准" : L"报告格式:推测(第三方VID)",
                    gplog::W(d.instanceId[0] ? d.instanceId : d.path));
    }
    if (g_count == 0) {
        

        GP_LOG_INFO("gphid: 没有找到可用的手柄 HID 接口。可能的原因：");
        GP_LOG_INFO("gphid:   1) 没插手柄，或手柄只暴露 XInput 接口不暴露 HID；");
        GP_LOG_INFO("gphid:   2) 手柄的厂商 ID 不在允许名单里（当前名单见 gpxinput.ini");
        GP_LOG_INFO("gphid:      的 HidVendorIds，默认只有微软 0x045E）；");
        GP_LOG_INFO("gphid:   3) 手柄的 HID 接口没有输出报告，物理上就收不了震动。");
        GP_LOG_INFO("gphid: 这不影响两个马达 —— 它们走 XInput，任何时候都可用。");
        GP_LOG_INFO("gphid: 要诊断具体是哪种情况，用 gp_vibtest.exe --scan。");
    }

    LeaveCriticalSection(&g_lock);
    return g_count;
}

void Shutdown(void) {
    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count; ++i) CloseDevice(&g_devices[i]);
    g_count = 0;
    LeaveCriticalSection(&g_lock);
}

int Rescan(void) {
    return Init();
}

int Count(void) { return g_count; }

const wchar_t* Describe(int index) {
    if (index < 0 || index >= g_count) return L"";
    return g_devices[index].path;
}

bool IsBluetooth(int index) {
    if (index < 0 || index >= g_count) return false;
    return g_devices[index].bluetooth != FALSE;
}

bool Send(int index, BYTE leftMotor, BYTE rightMotor,
          BYTE leftTrigger, BYTE rightTrigger, int pulseMode) {
    if (index < 0 || index >= g_count) return false;

    Device& d = g_devices[index];
    if (!d.hDevice || d.hDevice == INVALID_HANDLE_VALUE) return false;

    




    BYTE report[9];
    report[0] = d.reportId;
    report[1] = 0x0F;                    
    report[2] = leftTrigger;
    report[3] = rightTrigger;
    report[4] = leftMotor;
    report[5] = rightMotor;
    report[6] = pulseMode ? 128 : 255;   
    report[7] = pulseMode ? 128 : 0;     
    report[8] = 255;                     

    

    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(d.hDevice, report, sizeof(report), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        ok = GetOverlappedResult(d.hDevice, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);

    if (!ok) {
        DWORD err = GetLastError();
        GP_LOG_DEBUG("gphid: WriteFile 失败 (err=%lu)，设备可能已拔出，稍后重扫", err);
        

        CloseDevice(&d);
        return false;
    }
    return true;
}

bool SendMapped(int controllerIndex, BYTE leftMotor, BYTE rightMotor,
                BYTE leftTrigger, BYTE rightTrigger, int pulseMode) {
    if (g_count == 0) return false;

    


    int idx = controllerIndex % g_count;
    if (idx < 0) idx += g_count;

    if (!Send(idx, leftMotor, rightMotor, leftTrigger, rightTrigger, pulseMode)) {
        

        Rescan();
        if (g_count == 0) return false;
        idx = controllerIndex % g_count;
        return Send(idx, leftMotor, rightMotor, leftTrigger, rightTrigger, pulseMode);
    }
    return true;
}


int IoctlCount(void) {
    int n = 0;
    for (int i = 0; i < g_count && i < kMaxDevices; ++i) {
        if (g_devices[i].ioctlKind != 0) ++n;
    }
    return n;
}


bool SendIoctlAll(BYTE leftMotor, BYTE rightMotor,
                  BYTE leftTrigger, BYTE rightTrigger, int pulseMode) {
    for (int i = 0; i < g_count && i < kMaxDevices; ++i) {
        Device& d = g_devices[i];
        if (d.ioctlKind == 0) continue;
        if (!d.hDevice || d.hDevice == INVALID_HANDLE_VALUE) continue;

        DWORD ret = 0;
        BOOL ok = FALSE;
        if (d.ioctlKind == 1) {
            

            BYTE p7[7];
            p7[0] = leftTrigger;
            p7[1] = rightTrigger;
            p7[2] = leftMotor;
            p7[3] = rightMotor;
            p7[4] = pulseMode ? 128 : 255;   
            p7[5] = pulseMode ? 128 : 0;     
            p7[6] = 255;                     
            ok = DeviceIoControl(d.hDevice, 0x002aac08, p7, 7,
                                 nullptr, 0, &ret, nullptr);
        } else {
            

            BYTE p4[4] = {0, 0, leftMotor, rightMotor};
            ok = DeviceIoControl(d.hDevice, 0x8000a010, p4, 4,
                                 nullptr, 0, &ret, nullptr);
        }
        if (ok) return true;

        
        DWORD err = GetLastError();
        GP_LOG_INFO("gphid: IOCTL 直写失败 (err=%lu)，改用其它通道承载扳机", err);
        d.ioctlKind = 0;
    }
    return false;
}

}  
