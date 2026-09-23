// dllmain
#include <windows.h>

#include "gp_real.h"
#include "gp_log.h"
#include "gp_engine.h"
#include "gp_hooks.h"
#include "gp_config.h"

namespace {

HMODULE g_selfModule = nullptr;



void LogBanner(void) {
    wchar_t hostPath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, hostPath, MAX_PATH);

    GP_LOG_INFO("======================================================");
    GP_LOG_INFO("gpxinput 手柄震动反向代理 v2.0");
    GP_LOG_INFO("  冒充的模块 : %s", gplog::W(gpreal::SelfName()));
    GP_LOG_INFO("  自身路径   : %s", gplog::W(gpreal::SelfPath()));
    GP_LOG_INFO("  宿主进程   : %s (pid=%lu)", gplog::W(hostPath), GetCurrentProcessId());
    GP_LOG_INFO("  真身路径   : %s",
                gplog::W(gpreal::Ready() ? gpreal::RealPath() : L"(加载失败)"));
    


    GP_LOG_INFO("  日志文件   : %s", gplog::PathUtf8());
    GP_LOG_INFO("  构建时间   : %s %s", __DATE__, __TIME__);

    


    const wchar_t* selfPath = gpreal::SelfPath();
    const wchar_t* slash = wcsrchr(selfPath, L'\\');
    if (slash) {
        size_t selfDirLen = (size_t)(slash - selfPath) + 1;
        if (_wcsnicmp(hostPath, selfPath, selfDirLen) == 0) {
            GP_LOG_INFO("  同目录     : 是（与宿主 exe 同目录）");
        } else {
            GP_LOG_INFO("  同目录     : 否（与宿主不同目录）");
        }
    }

    if (gpreal::Injected()) {
        GP_LOG_INFO("  部署方式   : 注入 —— 拦截依赖挂钩真身函数，");
        GP_LOG_INFO("               我们的导出没有东西导入，属于预期情况。");
    } else {
        GP_LOG_INFO("  部署方式   : DLL 劫持 —— 游戏从导入表直接调用我们。");
    }

    GP_LOG_INFO("  进程位数   : %s", sizeof(void*) == 8 ? "x64" : "x86");
    GP_LOG_INFO("======================================================");
}

DWORD WINAPI InitThread(LPVOID) {
    LogBanner();

    if (gpreal::SelfLoadDetected()) {
        


        GP_LOG_ERROR("初始化中止：检测到自我递归。代理被安装到了系统目录，");
        GP_LOG_ERROR("请把它从 System32 移除，改放到游戏 exe 所在目录。");
        return 0;
    }

    if (!gpreal::Ready()) {
        GP_LOG_ERROR("初始化中止：真身加载失败，所有调用会返回安全错误码，");
        GP_LOG_ERROR("手柄在游戏里将完全没有震动。请确认 System32 下的 xinput DLL 未被破坏。");
        return 0;
    }

    

    GP_LOG_DEBUG("初始化: 进入 engine::Start");
    gp_engine::Start();
    GP_LOG_DEBUG("初始化: engine::Start 完成，进入 hooks::Install");
    gphooks::Install();
    GP_LOG_DEBUG("初始化: hooks::Install 完成");

    if (!gphooks::Installed()) {
        if (gpreal::Injected()) {
            
            GP_LOG_ERROR("注意：注入形态下挂钩是唯一的捕获手段，挂钩失败意味着");
            GP_LOG_ERROR("拦截完全不生效。请检查 gpxinput.ini 里的 HookRealXInput 是否为 true。");
        } else {
            GP_LOG_ERROR("注意：挂钩未能全部安装。我们的导出仍能拦截游戏通过导入表");
            GP_LOG_ERROR("发起的调用，但绕过我们的调用（直连真身、或走驱动 IOCTL）看不到。");
        }
    }

    return 0;
}

}  





extern "C" DWORD WINAPI gp_exp_XInputSetState(DWORD dwUserIndex,
                                              GpXInputVibration* pVibration) {
    WORD l = 0, r = 0;
    if (pVibration) {
        l = pVibration->wLeftMotorSpeed;
        r = pVibration->wRightMotorSpeed;
    }

    






    return gp_engine::OnSetState(dwUserIndex, l, r, gpreal::SetStateCall());
}





BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        g_selfModule = hModule;

        

        DisableThreadLibraryCalls(hModule);

        

        gplog::Init(nullptr);

        
        gpreal::Load();

        










        {
            GpProxyConfig early;
            GpConfigLoad(&early);
            gplog::SetLevel((gplog::Level)early.logLevel);
            gpreal::SetForceFfb(early.forceFfbCapability != FALSE);
        }

        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (h) {
            CloseHandle(h);
        } else {
            GP_LOG_ERROR("DllMain: 创建初始化线程失败 (err=%lu)，拦截不会生效",
                         GetLastError());
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        


        if (lpReserved == nullptr) {
            gphooks::Uninstall();
            gp_engine::Stop();
            gplog::Shutdown();
        }
        break;

    default:
        break;
    }

    return TRUE;
}
