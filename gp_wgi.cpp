// wgi
#include "gp_wgi.h"
#include "gp_log.h"

#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.h>

namespace {

using namespace ABI::Windows::Gaming::Input;
using ABI::Windows::Foundation::Collections::IVectorView;





typedef IVectorView<Gamepad*> GamepadView;


const DWORD kRefreshMs = 5000;

IGamepadStatics* volatile g_statics = nullptr;
IGamepad*        volatile g_pad     = nullptr;
volatile LONG g_count    = 0;      
volatile LONG g_sent     = 0;
volatile LONG g_started  = 0;      
DWORD         g_lastRefresh = 0;
BOOL          g_warned   = FALSE;

void ReleasePad(void) {
    if (g_pad) {
        g_pad->Release();
        g_pad = nullptr;
    }
    InterlockedExchange(&g_count, 0);
}


void Refresh(void) {
    if (!g_statics) return;

    GamepadView* view = nullptr;
    HRESULT hr = g_statics->get_Gamepads(&view);
    if (FAILED(hr) || !view) {
        GP_LOG_DEBUG("wgi: get_Gamepads 失败 hr=0x%08lX", (unsigned long)hr);
        ReleasePad();
        return;
    }

    unsigned n = 0;
    view->get_Size(&n);
    if (n == 0) {
        
        if (g_pad) GP_LOG_INFO("wgi: 手柄已断开，退回 XInput 通道");
        ReleasePad();
        view->Release();
        return;
    }

    if (g_pad) {
        
        InterlockedExchange(&g_count, (LONG)n);
        view->Release();
        return;
    }

    IGamepad* pad = nullptr;
    hr = view->GetAt(0, &pad);
    view->Release();
    if (FAILED(hr) || !pad) {
        GP_LOG_DEBUG("wgi: 取第一个手柄失败 hr=0x%08lX", (unsigned long)hr);
        return;
    }

    g_pad = pad;
    InterlockedExchange(&g_count, 1);
    GP_LOG_INFO("wgi: 通道可用（手柄 %u 个）—— 扳机走这条路，不需要管理员权限", n);
}

void StartOnce(void) {
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return;

    

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        if (!g_warned) {
            g_warned = TRUE;
            GP_LOG_INFO("wgi: WinRT 初始化失败 hr=0x%08lX —— 这条通道不可用，"
                        "退回 HID/XInput", (unsigned long)hr);
        }
        return;
    }

    HSTRING cls = nullptr;
    const wchar_t* name = RuntimeClass_Windows_Gaming_Input_Gamepad;
    if (FAILED(WindowsCreateString(name, (UINT32)wcslen(name), &cls))) return;

    IGamepadStatics* st = nullptr;
    hr = RoGetActivationFactory(cls, __uuidof(IGamepadStatics), (void**)&st);
    if (FAILED(hr) || !st) {
        if (!g_warned) {
            g_warned = TRUE;
            GP_LOG_INFO("wgi: 取 Gamepad 工厂失败 hr=0x%08lX —— 这条通道不可用，"
                        "退回 HID/XInput", (unsigned long)hr);
        }
        return;
    }

    g_statics = st;
    Refresh();
}

}  

namespace gpwgi {

void Tick(void) {
    if (!g_started) StartOnce();
    if (!g_statics) return;

    DWORD now = GetTickCount();
    if (g_pad && (DWORD)(now - g_lastRefresh) < kRefreshMs) return;
    g_lastRefresh = now;
    Refresh();
}

int Count(void) {
    return (int)g_count;
}

BOOL Set(BYTE leftMotor, BYTE rightMotor, BYTE leftTrigger, BYTE rightTrigger) {
    IGamepad* pad = g_pad;
    if (!pad) return FALSE;

    GamepadVibration v;
    
    v.LeftMotor    = leftMotor   / 255.0;
    v.RightMotor   = rightMotor  / 255.0;
    v.LeftTrigger  = leftTrigger / 255.0;
    v.RightTrigger = rightTrigger / 255.0;

    HRESULT hr = pad->put_Vibration(v);
    if (FAILED(hr)) {
        
        GP_LOG_DEBUG("wgi: put_Vibration 失败 hr=0x%08lX，重新枚举", (unsigned long)hr);
        ReleasePad();
        return FALSE;
    }
    InterlockedIncrement(&g_sent);
    return TRUE;
}

long SentCount(void) {
    return (long)g_sent;
}

}  
