// real
#include "gp_real.h"
#include "gp_log.h"

#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <ctype.h>

namespace gpreal {
namespace {

HMODULE  g_real        = nullptr;
bool     g_ready       = false;
bool     g_selfLoad    = false;
bool     g_injected    = false;
bool     g_forceFfb    = false;
wchar_t  g_realPath[MAX_PATH] = {0};
wchar_t  g_selfPath[MAX_PATH] = {0};
wchar_t  g_selfName[MAX_PATH] = {0};

GpFnGetState               g_getState              = nullptr;
GpFnSetState               g_setStateDirect        = nullptr;
GpFnSetState               g_setStateCall          = nullptr;
GpFnGetCapabilities        g_getCapabilities       = nullptr;
GpFnEnable                 g_enable                = nullptr;
GpFnGetBatteryInformation  g_getBattery            = nullptr;
GpFnGetKeystroke           g_getKeystroke          = nullptr;
GpFnGetAudioDeviceIds      g_getAudioDeviceIds     = nullptr;
GpFnGetDSoundAudioDeviceGuids g_getDSoundGuids     = nullptr;

void* GetProc(HMODULE mod, LPCSTR name) {
    return mod ? (void*)GetProcAddress(mod, name) : nullptr;
}



bool GetSelfModule(HMODULE* out) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(reinterpret_cast<void*>(&Load)), &self)) {
        return false;
    }
    *out = self;
    return true;
}

void ToLower(wchar_t* s) {
    for (; *s; ++s) *s = (wchar_t)towlower(*s);
}

}  

bool Load(void) {
    if (g_ready || g_selfLoad) return g_ready;

    HMODULE self = nullptr;
    if (!GetSelfModule(&self)) {
        GP_LOG_ERROR("gpreal: 拿不到自身模块句柄，无法定位真身");
        return false;
    }
    GetModuleFileNameW(self, g_selfPath, MAX_PATH);

    
    wcsncpy_s(g_selfName, MAX_PATH, g_selfPath, _TRUNCATE);
    {
        wchar_t* slash = wcsrchr(g_selfName, L'\\');
        if (slash) wmemmove(g_selfName, slash + 1, wcslen(slash + 1) + 1);
        ToLower(g_selfName);
    }

    












    {
        static const wchar_t* kRealNames[] = {
            L"xinput1_1.dll", L"xinput1_2.dll", L"xinput1_3.dll",
            L"xinput1_4.dll", L"xinput9_1_0.dll",
        };
        BOOL isRealName = FALSE;
        for (int i = 0; i < (int)(sizeof(kRealNames) / sizeof(kRealNames[0])); ++i) {
            if (!_wcsicmp(g_selfName, kRealNames[i])) {
                isRealName = TRUE;
                break;
            }
        }

        g_injected = !isRealName;
        if (g_injected) {
            GP_LOG_INFO("gpreal: 自身名为 %s，不是 XInput 的正式名字，判定为注入形态；"
                        "真身改用 xinput1_4.dll", gplog::W(g_selfName));
            wcsncpy_s(g_selfName, MAX_PATH, L"xinput1_4.dll", _TRUNCATE);
        }
    }

    wchar_t sysDir[MAX_PATH] = {0};
    if (!GetSystemDirectoryW(sysDir, MAX_PATH)) {
        GP_LOG_ERROR("gpreal: GetSystemDirectory 失败 (err=%lu)", GetLastError());
        return false;
    }

    wchar_t target[MAX_PATH];
    _snwprintf_s(target, MAX_PATH, _TRUNCATE, L"%s\\%s", sysDir, g_selfName);

    

    HMODULE real = LoadLibraryExW(target, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!real) {
        

        real = LoadLibraryExW(target, nullptr, 0);
    }
    if (!real) {
        GP_LOG_ERROR("gpreal: 加载真身失败: %s (err=%lu)", gplog::W(target), GetLastError());
        return false;
    }

    


    if (real == self) {
        g_selfLoad = true;
        GP_LOG_ERROR("gpreal: 递归自检命中！真身路径与我们自身相同: %s", gplog::W(g_selfPath));
        GP_LOG_ERROR("gpreal: 这通常意味着代理被安装到了 System32 而不是游戏目录。");
        GP_LOG_ERROR("gpreal: 已停止全部转发，手柄将保持游戏原有行为。");
        return false;
    }
    GetModuleFileNameW(real, g_realPath, MAX_PATH);
    if (_wcsicmp(g_realPath, g_selfPath) == 0) {
        g_selfLoad = true;
        GP_LOG_ERROR("gpreal: 递归自检命中（路径比对）: %s", gplog::W(g_realPath));
        return false;
    }

    g_real = real;

    g_getState        = (GpFnGetState)       GetProc(real, "XInputGetState");
    g_setStateDirect  = (GpFnSetState)       GetProc(real, "XInputSetState");
    g_setStateCall    = g_setStateDirect;
    g_getCapabilities = (GpFnGetCapabilities)GetProc(real, "XInputGetCapabilities");
    g_enable          = (GpFnEnable)         GetProc(real, "XInputEnable");
    g_getBattery      = (GpFnGetBatteryInformation)GetProc(real, "XInputGetBatteryInformation");
    g_getKeystroke    = (GpFnGetKeystroke)   GetProc(real, "XInputGetKeystroke");
    g_getAudioDeviceIds = (GpFnGetAudioDeviceIds)GetProc(real, "XInputGetAudioDeviceIds");
    g_getDSoundGuids  = (GpFnGetDSoundAudioDeviceGuids)GetProc(real, "XInputGetDSoundAudioDeviceGuids");

    
    if (!g_setStateDirect) {
        GP_LOG_ERROR("gpreal: 真身里找不到 XInputSetState，代理无法工作");
        return false;
    }

    g_ready = true;
    GP_LOG_INFO("gpreal: 真身已加载 self=%s -> real=%s",
                gplog::W(g_selfPath), gplog::W(g_realPath));
    return true;
}

bool        SelfLoadDetected(void)      { return g_selfLoad; }
bool        Injected(void)              { return g_injected; }
void        SetForceFfb(bool on)        { g_forceFfb = on; }
bool        ForceFfb(void)              { return g_forceFfb; }
const wchar_t* RealPath(void)           { return g_realPath; }
const wchar_t* SelfPath(void)           { return g_selfPath; }
const wchar_t* SelfName(void)           { return g_selfName; }
bool        Ready(void)                 { return g_ready; }

GpFnSetState SetStateDirect(void)       { return g_setStateDirect; }
GpFnSetState SetStateCall(void)         { return g_setStateCall; }
void SetSetStateTrampoline(GpFnSetState t) { g_setStateCall = t ? t : g_setStateDirect; }

GpFnGetState               GetState(void)              { return g_getState; }
GpFnGetCapabilities        GetCapabilities(void)       { return g_getCapabilities; }
GpFnEnable                 EnableFn(void)              { return g_enable; }
GpFnGetBatteryInformation  GetBatteryInformation(void) { return g_getBattery; }
GpFnGetKeystroke           GetKeystroke(void)          { return g_getKeystroke; }
GpFnGetAudioDeviceIds      GetAudioDeviceIds(void)     { return g_getAudioDeviceIds; }
GpFnGetDSoundAudioDeviceGuids GetDSoundAudioDeviceGuids(void) { return g_getDSoundGuids; }

void* ResolveOrdinal(WORD ordinal) {
    if (!g_real) return nullptr;
    return (void*)GetProcAddress(g_real, MAKEINTRESOURCEA(ordinal));
}

}  








namespace {
const DWORD kErrorDeviceNotConnected = 1167;  
}

extern "C" {

BOOL WINAPI gp_exp_DllMain(HINSTANCE, DWORD, LPVOID) {
    

    return TRUE;
}

DWORD WINAPI gp_exp_XInputGetState(DWORD dwUserIndex, GpXInputState* pState) {
    GpFnGetState fn = gpreal::GetState();
    return fn ? fn(dwUserIndex, pState) : kErrorDeviceNotConnected;
}

DWORD WINAPI gp_exp_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags,
                                          GpXInputCapabilities* pCapabilities) {
    GpFnGetCapabilities fn = gpreal::GetCapabilities();
    if (!fn) return kErrorDeviceNotConnected;

    DWORD rc = fn(dwUserIndex, dwFlags, pCapabilities);

    







    if (rc == ERROR_SUCCESS && pCapabilities) {
        BOOL ffb = (pCapabilities->Flags & 0x0001) != 0;
        GP_LOG_INFO("gpreal: XInputGetCapabilities(槽位%lu) 返回 Flags=0x%04X"
                    " -> 游戏认为本手柄%s震动%s",
                    dwUserIndex, pCapabilities->Flags,
                    ffb ? "支持" : "**不支持**",
                    ffb ? "" : "（游戏很可能因此全程不发震动）");

        




        if (!ffb && gpreal::ForceFfb()) {
            pCapabilities->Flags |= 0x0001;
            GP_LOG_INFO("gpreal: 已按配置把 FFB 位补上 -> Flags=0x%04X",
                        pCapabilities->Flags);
        }
    }
    return rc;
}

void WINAPI gp_exp_XInputEnable(BOOL enable) {
    GpFnEnable fn = gpreal::EnableFn();
    if (fn) fn(enable);
}

DWORD WINAPI gp_exp_XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType,
                                                GpXInputBatteryInformation* pBatteryInformation) {
    GpFnGetBatteryInformation fn = gpreal::GetBatteryInformation();
    return fn ? fn(dwUserIndex, devType, pBatteryInformation) : kErrorDeviceNotConnected;
}

DWORD WINAPI gp_exp_XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved,
                                       GpXInputKeystroke* pKeystroke) {
    GpFnGetKeystroke fn = gpreal::GetKeystroke();
    return fn ? fn(dwUserIndex, dwReserved, pKeystroke) : kErrorDeviceNotConnected;
}

DWORD WINAPI gp_exp_XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId,
                                            UINT* pRenderCount, LPWSTR pCaptureDeviceId,
                                            UINT* pCaptureCount) {
    GpFnGetAudioDeviceIds fn = gpreal::GetAudioDeviceIds();
    return fn ? fn(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount)
              : kErrorDeviceNotConnected;
}

DWORD WINAPI gp_exp_XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex,
                                                    GUID* pDSoundRenderGuid,
                                                    GUID* pDSoundCaptureGuid) {
    GpFnGetDSoundAudioDeviceGuids fn = gpreal::GetDSoundAudioDeviceGuids();
    return fn ? fn(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid)
              : kErrorDeviceNotConnected;
}



namespace {

void* CachedOrdinal(WORD ordinal, void** cache) {
    if (!*cache) *cache = gpreal::ResolveOrdinal(ordinal);
    return *cache;
}

void* g_ord100 = nullptr;
void* g_ord101 = nullptr;
void* g_ord102 = nullptr;
void* g_ord103 = nullptr;
void* g_ord104 = nullptr;
void* g_ord108 = nullptr;
void* g_ord109 = nullptr;

const DWORD kErrorProcNotFound = 127;  
}  

DWORD WINAPI gp_exp_ordinal100(DWORD dwUserIndex, GpXInputState* pState) {
    GpFnGetStateEx fn = (GpFnGetStateEx)CachedOrdinal(100, &g_ord100);
    return fn ? fn(dwUserIndex, pState) : kErrorDeviceNotConnected;
}

DWORD WINAPI gp_exp_ordinal101(DWORD a, DWORD b, void* c) {
    typedef DWORD (WINAPI *Fn)(DWORD, DWORD, void*);
    Fn fn = (Fn)CachedOrdinal(101, &g_ord101);
    return fn ? fn(a, b, c) : kErrorProcNotFound;
}

DWORD WINAPI gp_exp_ordinal102(DWORD a) {
    typedef DWORD (WINAPI *Fn)(DWORD);
    Fn fn = (Fn)CachedOrdinal(102, &g_ord102);
    return fn ? fn(a) : kErrorProcNotFound;
}

DWORD WINAPI gp_exp_ordinal103(DWORD a) {
    typedef DWORD (WINAPI *Fn)(DWORD);
    Fn fn = (Fn)CachedOrdinal(103, &g_ord103);
    return fn ? fn(a) : kErrorProcNotFound;
}

DWORD WINAPI gp_exp_ordinal104(DWORD a, void* b) {
    typedef DWORD (WINAPI *Fn)(DWORD, void*);
    Fn fn = (Fn)CachedOrdinal(104, &g_ord104);
    return fn ? fn(a, b) : kErrorProcNotFound;
}

DWORD WINAPI gp_exp_ordinal108(DWORD a, DWORD b, DWORD c, void* d) {
    typedef DWORD (WINAPI *Fn)(DWORD, DWORD, DWORD, void*);
    Fn fn = (Fn)CachedOrdinal(108, &g_ord108);
    return fn ? fn(a, b, c, d) : kErrorProcNotFound;
}

DWORD WINAPI gp_exp_ordinal109(void* a, void* b, void* c, void* d) {
    typedef DWORD (WINAPI *Fn)(void*, void*, void*, void*);
    Fn fn = (Fn)CachedOrdinal(109, &g_ord109);
    return fn ? fn(a, b, c, d) : kErrorProcNotFound;
}

}  
