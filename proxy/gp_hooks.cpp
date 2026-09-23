// hooks
#include "gp_hooks.h"
#include "gp_wgihook.h"
#include "gp_engine.h"
#include "gp_hid.h"
#include "gp_config.h"
#include "gp_log.h"

#include "MinHook.h"

namespace gphooks {
namespace {

GpFnSetState g_trampoline = nullptr;
bool         g_installed  = false;

typedef BOOL (WINAPI *FnDeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD,
                                         LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
FnDeviceIoControl g_realDeviceIoControl = nullptr;







typedef BOOL (WINAPI *FnWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
FnWriteFile g_realWriteFile = nullptr;



bool g_mhReady = false;

const char* MhErrorName(MH_STATUS st) {
    switch (st) {
    case MH_OK:                    return "MH_OK";
    case MH_ERROR_ALREADY_INITIALIZED:  return "已初始化过";
    case MH_ERROR_NOT_INITIALIZED:      return "未初始化";
    case MH_ERROR_ALREADY_CREATED:      return "该地址已被挂钩";
    case MH_ERROR_NOT_CREATED:          return "该地址没有挂钩";
    case MH_ERROR_ENABLED:              return "已启用";
    case MH_ERROR_DISABLED:             return "未启用";
    case MH_ERROR_NOT_EXECUTABLE:       return "目标地址不可执行";
    case MH_ERROR_UNSUPPORTED_FUNCTION: return "函数开头不支持挂钩";
    case MH_ERROR_MEMORY_ALLOC:         return "内存分配失败";
    case MH_ERROR_MEMORY_PROTECT:       return "内存属性修改失败";
    case MH_ERROR_MODULE_NOT_FOUND:     return "找不到模块";
    case MH_ERROR_FUNCTION_NOT_FOUND:   return "找不到函数";
    default:                            return "未知错误";
    }
}







void* ResolveKernel32(const char* name) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) k32 = LoadLibraryW(L"kernel32.dll");
    if (!k32) return nullptr;
    return (void*)GetProcAddress(k32, name);
}

DWORD WINAPI DetourXInputSetState(DWORD dwUserIndex, GpXInputVibration* pVibration) {
    WORD l = 0, r = 0;
    if (pVibration) {
        l = pVibration->wLeftMotorSpeed;
        r = pVibration->wRightMotorSpeed;
    }
    

    return gp_engine::OnSetState(dwUserIndex, l, r, g_trampoline);
}

BOOL WINAPI DetourWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nBytes,
                            LPDWORD lpWritten, LPOVERLAPPED lpOverlapped) {
    if (!gphid::IsOurHandle(hFile)) {
        return g_realWriteFile(hFile, lpBuffer, nBytes, lpWritten, lpOverlapped);
    }

    
    if (gp_engine::OnNativeHidWrite(hFile, lpBuffer, nBytes)) {
        if (lpWritten) *lpWritten = nBytes;   
        return TRUE;
    }
    return g_realWriteFile(hFile, lpBuffer, nBytes, lpWritten, lpOverlapped);
}

BOOL WINAPI DetourDeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
                                  LPVOID lpInBuffer, DWORD nInBufferSize,
                                  LPVOID lpOutBuffer, DWORD nOutBufferSize,
                                  LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped) {
    

    

    gp_engine::SetHookCheckHandle(hDevice);
    if (!lpOverlapped && !gp_engine::IsSelfWrite(hDevice) &&
        gp_engine::OnDeviceIoControl(dwIoControlCode, lpInBuffer, nInBufferSize)) {
        if (lpBytesReturned) *lpBytesReturned = 0;
        return TRUE;   
    }
    return g_realDeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize,
                                 lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);
}




















typedef void* (__cdecl *FnCreateInterface)(const char* version);
typedef void* (__cdecl *FnFindOrCreateInterface)(void* hSteamUser, const char* version);

FnCreateInterface        g_realCreateInterface = nullptr;
FnFindOrCreateInterface  g_realFindOrCreate    = nullptr;






BOOL LooksLikeControllerInterface(const char* version) {
    if (!version) return FALSE;
    if (strstr(version, "SteamInput")) return TRUE;
    if (strstr(version, "SteamController")) return TRUE;
    if (strstr(version, "Controller")) return TRUE;
    return FALSE;
}

void DumpVtable(const char* tag, void* obj) {
    if (!obj) return;
    void** vt = *(void***)obj;
    if (!vt) return;

    

    GP_LOG_INFO("steam: %s 的 vtable 指针 = %p", tag, (void*)vt);
    for (int i = 0; i < 48; i += 6) {
        GP_LOG_INFO("steam:   [%02d..%02d] %p %p %p %p %p %p",
                    i, i + 5, vt[i], vt[i + 1], vt[i + 2],
                    vt[i + 3], vt[i + 4], vt[i + 5]);
    }
}

void* __cdecl DetourCreateInterface(const char* version) {
    void* r = g_realCreateInterface ? g_realCreateInterface(version) : nullptr;
    if (version) {
        GP_LOG_INFO("steam: CreateInterface(\"%s\") -> %p", version, r);
        if (LooksLikeControllerInterface(version)) {
            GP_LOG_INFO("steam: >>> 这个游戏用的是 Steam 手柄接口，震动不走 XInput <<<");
            DumpVtable("Steam 手柄接口", r);
        }
    }
    return r;
}

void* __cdecl DetourFindOrCreateInterface(void* hSteamUser, const char* version) {
    void* r = g_realFindOrCreate ? g_realFindOrCreate(hSteamUser, version) : nullptr;
    if (version) {
        GP_LOG_INFO("steam: FindOrCreateUserInterface(\"%s\") -> %p", version, r);
        if (LooksLikeControllerInterface(version)) {
            GP_LOG_INFO("steam: >>> 这个游戏用的是 Steam 手柄接口，震动不走 XInput <<<");
            DumpVtable("Steam 手柄接口", r);
        }
    }
    return r;
}



int InstallSteamProbe(void) {
    HMODULE h = GetModuleHandleW(L"steam_api64.dll");
    if (!h) h = GetModuleHandleW(L"steam_api.dll");
    if (!h) {
        GP_LOG_INFO("steam: 进程里没有 steam_api64.dll，跳过 Steam 探针");
        return 0;
    }

    int n = 0;
    void* p1 = (void*)GetProcAddress(h, "SteamInternal_CreateInterface");
    if (p1 && MH_CreateHook(p1, (LPVOID)&DetourCreateInterface,
                            (LPVOID*)&g_realCreateInterface) == MH_OK) {
        ++n;
    }
    void* p2 = (void*)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
    if (p2 && MH_CreateHook(p2, (LPVOID)&DetourFindOrCreateInterface,
                            (LPVOID*)&g_realFindOrCreate) == MH_OK) {
        ++n;
    }

    GP_LOG_INFO("steam: Steam 接口探针已安装 %d 个挂钩", n);
    if (n > 0) {
        GP_LOG_INFO("steam: 注意 —— 本进程用到了 Steam 输入接口。**请在该游戏的 Steam "
                    "属性里关闭 Steam 输入**，否则原生震动会经 Steam 直达手柄，"
                    "绕过我们的拦截（这一路我们不挂钩）");
    }
    return n;
}

}  

bool Install(void) {
    if (g_installed) return true;

    
    if (gp_engine::SecondaryInstance()) {
        GP_LOG_INFO("hooks: 从属实例，跳过全部挂钩（主实例负责捕获与输出）");
        return false;
    }

    GpProxyConfig cfg;
    GpConfigDefaults(&cfg);
    GpConfigLoad(&cfg);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        GP_LOG_ERROR("hooks: MH_Initialize 失败: %s", MhErrorName(st));
        return false;
    }
    g_mhReady = true;

    int created = 0;

    





    if (cfg.hookRealXInput) {
        void* target = (void*)gpreal::SetStateDirect();
        if (target) {
            st = MH_CreateHook(target, (LPVOID)&DetourXInputSetState, (LPVOID*)&g_trampoline);
            if (st == MH_OK) {
                gpreal::SetSetStateTrampoline(g_trampoline);
                ++created;
                GP_LOG_INFO("hooks: 已挂钩真实 XInputSetState @ %p", target);
            } else {
                GP_LOG_ERROR("hooks: 挂钩 XInputSetState 失败: %s", MhErrorName(st));
            }
        } else {
            GP_LOG_ERROR("hooks: 拿不到 XInputSetState 地址，跳过");
        }
    } else {
        GP_LOG_INFO("hooks: 按配置跳过真实 XInputSetState 挂钩");
    }

    



    if (cfg.hookDeviceIoControl) {
        void* target = ResolveKernel32("DeviceIoControl");
        if (target) {
            st = MH_CreateHook(target, (LPVOID)&DetourDeviceIoControl,
                               (LPVOID*)&g_realDeviceIoControl);
            if (st == MH_OK) {
                ++created;
                GP_LOG_INFO("hooks: 已挂钩 DeviceIoControl @ %p (kernel32)", target);
            } else {
                GP_LOG_ERROR("hooks: 挂钩 DeviceIoControl 失败: %s", MhErrorName(st));
            }
        } else {
            GP_LOG_ERROR("hooks: 拿不到 kernel32!DeviceIoControl 地址，跳过");
        }
    } else {
        GP_LOG_INFO("hooks: 按配置跳过 DeviceIoControl 挂钩");
    }

    



    {
        void* target = ResolveKernel32("WriteFile");
        if (target) {
            st = MH_CreateHook(target, (LPVOID)&DetourWriteFile, (LPVOID*)&g_realWriteFile);
            if (st == MH_OK) {
                ++created;
                GP_LOG_INFO("hooks: 已挂钩 WriteFile @ %p (kernel32) —— 原生 HID 写也会被拦", target);
            } else {
                GP_LOG_ERROR("hooks: 挂钩 WriteFile 失败: %s", MhErrorName(st));
            }
        }
    }

    

    created += InstallSteamProbe();

    


    if (gpwgihook::Install()) ++created;

    if (created > 0) {
        

        st = MH_EnableHook(MH_ALL_HOOKS);
        if (st != MH_OK) {
            GP_LOG_ERROR("hooks: MH_EnableHook 失败: %s —— 挂钩未生效", MhErrorName(st));
            MH_DisableHook(MH_ALL_HOOKS);
            return false;
        }
        g_installed = true;
        GP_LOG_INFO("hooks: %d 个挂钩已启用", created);
    } else {
        GP_LOG_ERROR("hooks: 没有任何挂钩被创建，拦截不会生效");
    }

    return g_installed;
}

void Uninstall(void) {
    if (!g_mhReady) return;
    if (g_installed) {
        MH_DisableHook(MH_ALL_HOOKS);
        g_installed = false;
    }
    MH_Uninitialize();
    g_mhReady = false;
    

    g_trampoline = nullptr;
    gpreal::SetSetStateTrampoline(nullptr);
}

bool Installed(void) { return g_installed; }
GpFnSetState Trampoline(void) { return g_trampoline; }

}  
