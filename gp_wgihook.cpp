



















#include "gp_wgihook.h"

#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.h>

#include <string.h>

#include "MinHook.h"
#include "gp_log.h"
#include "gp_ipc.h"
#include "gp_ipc_client.h"

#pragma comment(lib, "runtimeobject.lib")

namespace {

using namespace ABI::Windows::Gaming::Input;
using ABI::Windows::Foundation::Collections::IVectorView;



typedef IVectorView<Gamepad*> GamepadView;

typedef HRESULT (WINAPI *FnRoGetActivationFactory)(HSTRING, REFIID, void**);

FnRoGetActivationFactory g_realGetFactory = nullptr;
















static const size_t kSlotPutVibration = 7;
static const size_t kGamepadSlots     = 9;
static const size_t kSlotGetGamepads  = 10;
static const size_t kStaticsSlots     = 11;


bool LooksLikeWgiImpl(const void* fn) {
    if (!fn) return false;
    HMODULE mod = GetModuleHandleW(L"Windows.Gaming.Input.dll");
    if (!mod && !(mod = LoadLibraryW(L"Windows.Gaming.Input.dll"))) return false;

    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery(fn, &mbi, sizeof(mbi)) == 0) return false;
    return mbi.AllocationBase == (void*)mod;
}

HRESULT STDMETHODCALLTYPE HookPutVibration(IGamepad* self, GamepadVibration value);



const int kMaxPatches = 64;

struct VtablePatch {
    void**  object;      
    void**  copy;        
    size_t  slots;
    


    void*   origSlot;
    bool    used;
};

VtablePatch g_patches[kMaxPatches];
CRITICAL_SECTION g_patchLock;
volatile LONG g_lockReady = 0;




void* g_realPutVibration = nullptr;

void EnsureLock(void) {
    if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0) {
        InitializeCriticalSection(&g_patchLock);
    }
}



void* PatchSlot(void** object, size_t slotCount, size_t slot,
                void* replacement, void** outOriginal) {
    if (!object) return nullptr;
    EnsureLock();
    EnterCriticalSection(&g_patchLock);

    
    for (int i = 0; i < kMaxPatches; ++i) {
        if (g_patches[i].used && g_patches[i].object == object) {
            void* orig = g_patches[i].origSlot;
            LeaveCriticalSection(&g_patchLock);
            if (outOriginal) *outOriginal = orig;
            return orig;
        }
    }

    int idx = -1;
    for (int i = 0; i < kMaxPatches; ++i) {
        if (!g_patches[i].used) { idx = i; break; }
    }
    if (idx < 0) {
        LeaveCriticalSection(&g_patchLock);
        GP_LOG_ERROR("wgihook: 补丁表满了，这一轮不再挂钩新实例");
        return nullptr;
    }

    void** old = *(void***)object;
    if (!LooksLikeWgiImpl(old[slot])) {
        LeaveCriticalSection(&g_patchLock);
        GP_LOG_ERROR("wgihook: vtable 第 %u 项不像 WGI 的实现（槽位可能对不上），"
                     "这次不挂钩 —— 宁可少抓一条通道，也不能把宿主搞崩",
                     (unsigned)slot);
        return nullptr;
    }
    void** copy = (void**)malloc(slotCount * sizeof(void*));
    if (!copy) {
        LeaveCriticalSection(&g_patchLock);
        return nullptr;
    }
    memcpy(copy, old, slotCount * sizeof(void*));

    void* orig = copy[slot];
    copy[slot] = replacement;
    *(void***)object = copy;      

    

    if (orig == replacement) {
        copy[slot] = orig;
        free(copy);
        LeaveCriticalSection(&g_patchLock);
        GP_LOG_ERROR("wgihook: 原函数与替换函数相同，拒绝挂钩（内部错误）");
        return nullptr;
    }

    g_patches[idx].object = object;
    g_patches[idx].copy = copy;
    g_patches[idx].slots = slotCount;
    g_patches[idx].origSlot = orig;
    g_patches[idx].used = true;

    LeaveCriticalSection(&g_patchLock);
    if (outOriginal) *outOriginal = orig;
    return orig;
}





void LogWgiRaw(const BYTE* raw4, const GamepadVibration& v);





void PublishWgi(const GamepadVibration& v) {
    GpFrame f;
    memset(&f, 0, sizeof(f));
    f.tickMs = GetTickCount();
    f.pid = GetCurrentProcessId();
    f.controller = 0;                 
    f.source = GP_SRC_WGI;
    f.flags = GP_FRAME_F_FROM_GAME;
    f.conn = GP_CONN_USB_24G;
    f.leftMotor   = (float)v.LeftMotor;
    f.rightMotor  = (float)v.RightMotor;
    f.leftTrigger = (float)v.LeftTrigger;
    f.rightTrigger = (float)v.RightTrigger;
    f.rawLeftMotor    = (BYTE)(v.LeftMotor    <= 0 ? 0 : (v.LeftMotor    >= 1 ? 255 : (int)(v.LeftMotor    * 255 + 0.5)));
    f.rawRightMotor   = (BYTE)(v.RightMotor   <= 0 ? 0 : (v.RightMotor   >= 1 ? 255 : (int)(v.RightMotor   * 255 + 0.5)));
    f.rawLeftTrigger  = (BYTE)(v.LeftTrigger  <= 0 ? 0 : (v.LeftTrigger  >= 1 ? 255 : (int)(v.LeftTrigger  * 255 + 0.5)));
    f.rawRightTrigger = (BYTE)(v.RightTrigger <= 0 ? 0 : (v.RightTrigger >= 1 ? 255 : (int)(v.RightTrigger * 255 + 0.5)));
    if (f.rawLeftMotor == 0 && f.rawRightMotor == 0 &&
        f.rawLeftTrigger == 0 && f.rawRightTrigger == 0) {
        f.flags |= GP_FRAME_F_FULL_TICK;   
    }
    gpshm::Publish(&f);

    LogWgiRaw((const BYTE*)&f.rawLeftMotor, v);
}













volatile LONG g_wgiTotal = 0;
volatile LONG g_wgiWindow = 0;
DWORD g_lastSummaryTick = 0;
DWORD g_lastChangeTick = 0;
BYTE  g_lastRaw[4] = {0, 0, 0, 0};
BOOL  g_sawFirst = FALSE;

void LogWgiRaw(const BYTE* raw4, const GamepadVibration& v) {
    InterlockedIncrement(&g_wgiTotal);
    InterlockedIncrement(&g_wgiWindow);

    DWORD now = GetTickCount();

    if (!g_sawFirst) {
        g_sawFirst = TRUE;
        GP_LOG_INFO("wgihook: **收到第一帧 WGI 震动** —— 这条通道通了 "
                    "(LM=%u RM=%u LT=%u RT=%u)",
                    raw4[0], raw4[1], raw4[2], raw4[3]);
    }

    if (memcmp(raw4, g_lastRaw, 4) != 0 && (DWORD)(now - g_lastChangeTick) >= 200) {
        g_lastChangeTick = now;
        memcpy(g_lastRaw, raw4, 4);
        if (raw4[0] == 0 && raw4[1] == 0 && raw4[2] == 0 && raw4[3] == 0) {
            GP_LOG_INFO("wgihook: 震动停止（全零）");
        } else {
            GP_LOG_INFO("wgihook: 震动 LM=%u RM=%u LT=%u RT=%u",
                        raw4[0], raw4[1], raw4[2], raw4[3]);
        }
    }

    if ((DWORD)(now - g_lastSummaryTick) >= 5000) {
        g_lastSummaryTick = now;
        LONG w = InterlockedExchange(&g_wgiWindow, 0);
        GP_LOG_INFO("wgihook: 小计 —— 最近 5 秒 %ld 帧，累计 %ld 帧"
                    "（LT/RT 就是扳机那两路）", w, g_wgiTotal);
    }
    (void)v;
}



void* g_realGetGamepads = nullptr;

HRESULT STDMETHODCALLTYPE HookGetGamepads(IGamepadStatics* self, GamepadView** out) {
    if (!g_realGetGamepads) return E_FAIL;
    typedef HRESULT (STDMETHODCALLTYPE *Fn)(IGamepadStatics*, GamepadView**);
    HRESULT hr = ((Fn)g_realGetGamepads)(self, out);
    if (FAILED(hr) || !out || !*out) return hr;

    

    unsigned n = 0;
    (*out)->get_Size(&n);
    

    GP_LOG_INFO("wgihook: 程序查询手柄列表 -> %u 个%s", n,
                n == 0 ? "（认不到手柄：网页里请先按一下手柄按键）" : "");
    for (unsigned i = 0; i < n && i < 8; ++i) {
        IGamepad* pad = nullptr;
        if (FAILED((*out)->GetAt(i, &pad)) || !pad) continue;

        void* orig = nullptr;
        PatchSlot((void**)pad, kGamepadSlots, kSlotPutVibration,
                  (void*)&HookPutVibration, &orig);
        if (orig && !g_realPutVibration) g_realPutVibration = orig;

        

        pad->Release();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookPutVibration(IGamepad* self, GamepadVibration value) {
    PublishWgi(value);

    if (!g_realPutVibration) return E_FAIL;
    typedef HRESULT (STDMETHODCALLTYPE *Fn)(IGamepad*, GamepadVibration);
    return ((Fn)g_realPutVibration)(self, value);
}



HRESULT WINAPI DetourRoGetActivationFactory(HSTRING classId, REFIID iid, void** out) {
    HRESULT hr = g_realGetFactory(classId, iid, out);
    if (FAILED(hr) || !out || !*out) return hr;

    

    UINT32 len = 0;
    const wchar_t* name = WindowsGetStringRawBuffer(classId, &len);
    if (!name || !len) return hr;

    static const wchar_t* kGamepadClass = L"Windows.Gaming.Input.Gamepad";
    if (wcsncmp(name, kGamepadClass, len) != 0 && wcslen(kGamepadClass) != len) {
        return hr;                 
    }

    GP_LOG_INFO("wgihook: 看到程序取 Gamepad 运行时类，开始接管它的震动输出");

    void* orig = nullptr;
    PatchSlot((void**)*out, kStaticsSlots, kSlotGetGamepads, (void*)&HookGetGamepads, &orig);
    if (orig && !g_realGetGamepads) g_realGetGamepads = orig;
    return hr;
}

}  

namespace gpwgihook {

bool Install(void) {
    



    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (!combase) combase = LoadLibraryW(L"combase.dll");
    if (!combase) {
        GP_LOG_ERROR("wgihook: 拿不到 combase.dll，WGI 这条通道抓不到");
        return false;
    }

    void* target = (void*)GetProcAddress(combase, "RoGetActivationFactory");
    if (!target) {
        GP_LOG_ERROR("wgihook: 找不到 RoGetActivationFactory");
        return false;
    }

    MH_STATUS st = MH_CreateHook(target, (LPVOID)&DetourRoGetActivationFactory,
                                 (LPVOID*)&g_realGetFactory);
    if (st != MH_OK) {
        GP_LOG_ERROR("wgihook: 挂钩 RoGetActivationFactory 失败: %d", (int)st);
        return false;
    }

    GP_LOG_INFO("wgihook: 已挂钩 RoGetActivationFactory @ %p "
                "（构建 %s %s）—— Chrome/Edge 网页里的震动、以及用 "
                "Windows.Gaming.Input 的游戏（含扳机）都会被抓到",
                target, __DATE__, __TIME__);
    return true;
}

void Uninstall(void) {
    

}

}  
