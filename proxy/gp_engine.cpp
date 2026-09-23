// engine
#include "gp_engine.h"
#include "gp_ipc_client.h"
#include "gp_hid.h"
#include "gp_wgi.h"
#include "gp_config.h"
#include "gp_capture.h"
#include "gp_haptics.h"
#include "gp_log.h"
#include "gp_monitor.h"
#include "gp_gamestate.h"

#include <string.h>
#include <stdio.h>

namespace gp_engine {
namespace {




const DWORD kDedupWindowMs = 2;




const DWORD kResultStaleMs = 200;



const DWORD kKeepAliveMs = 500;

GpProxyConfig   g_cfg;
volatile LONG   g_running = 0;
HANDLE          g_outputThread = nullptr;
BOOL            g_secondary     = FALSE;   
volatile DWORD  g_outputThreadId = 0;   






__declspec(thread) int t_selfOutput = 0;
HANDLE          g_stopEvent = nullptr;


struct ControllerState {
    GpFrame lastGame;
    DWORD   lastGameTick;
    BOOL    sawGame;

    GpFrame lastSent;
    DWORD   lastSendTick;
    BOOL    sent;

    DWORD   lastDedupTick;
    BOOL    dedupValid;
    GpFrame lastPublished;

    






    








    BOOL    xinputTookOver;
    BOOL    xinputTakeoverLogged;

    

    GpFrame lastResult;
    BOOL    lastResultValid;
};
ControllerState g_ctrl[GPIPC_MAX_CONTROLLERS];

volatile LONG g_statCaptured = 0;
volatile LONG g_statBlocked  = 0;
volatile LONG g_statForward  = 0;
volatile LONG g_statTimeouts = 0;
volatile LONG g_statHidSent  = 0;


volatile LONG g_statSilenced = 0;






inline void BumpStat(volatile LONG* local, volatile uint32_t* shared) {
    InterlockedIncrement(local);
    if (shared) InterlockedIncrement((volatile LONG*)shared);
}

inline void BumpForward(void) {
    GpControl* c = gpshm::Control();
    BumpStat(&g_statForward, c ? &c->statFramesOut : nullptr);
}

inline void BumpBlocked(void) {
    GpControl* c = gpshm::Control();
    BumpStat(&g_statBlocked, c ? &c->statBlocked : nullptr);
}

inline void BumpTimeout(void) {
    GpControl* c = gpshm::Control();
    BumpStat(&g_statTimeouts, c ? &c->statTimeouts : nullptr);
}










GpPolicy ActivePolicy(void) {
    GpPolicy p = gpshm::EffectivePolicy();
    if (!g_cfg.haptics.enable) return p;

    int m = g_cfg.mode;
    if (m < 0) m = 0;
    if (m > 2) m = 2;
    GpPolicy self = (GpPolicy)m;
    if (self != GP_POLICY_PASSTHROUGH) return self;
    return p;
}



inline BYTE ToRaw(WORD v) {
    return (BYTE)((v + 128) / 257);
}
inline WORD FromRaw(BYTE r) {
    return (WORD)(r * 257);
}


inline BYTE ClampToByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (BYTE)(v + 0.5f);
}







BYTE g_lastPadLT[4] = {0};
BYTE g_lastPadRT[4] = {0};

void ReadPadTriggers(uint32_t controller, BYTE* lt, BYTE* rt) {
    if (!lt || !rt) return;

    GpFnGetState fn = gpreal::GetState();
    if (!fn) { *lt = g_lastPadLT[controller & 3]; *rt = g_lastPadRT[controller & 3]; return; }

    GpXInputState st;
    memset(&st, 0, sizeof(st));
    if (fn(controller, &st) == ERROR_SUCCESS) {
        *lt = st.Gamepad.bLeftTrigger;
        *rt = st.Gamepad.bRightTrigger;
    } else {
        




        *lt = g_lastPadLT[controller & 3];
        *rt = g_lastPadRT[controller & 3];
        return;
    }
    g_lastPadLT[controller & 3] = *lt;
    g_lastPadRT[controller & 3] = *rt;
}









GpMonBlock* g_mon = nullptr;
HANDLE      g_monMapping = nullptr;

struct MonSample {
    BOOL hasGame;
    BOOL padValid;
    BYTE gameLM, gameRM, gameLT, gameRT;
    BYTE outLM, outRM, outLT, outRT;
    BYTE padLT, padRT;
};

void MonitorInit(void) {
    if (!g_cfg.monitorEnable) return;

    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, GPMON_NAME);
    BOOL created = FALSE;
    if (!h) {
        h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               sizeof(GpMonBlock), GPMON_NAME);
        if (h && GetLastError() != ERROR_ALREADY_EXISTS) created = TRUE;
    }
    if (!h) {
        GP_LOG_DEBUG("monitor: 创建/打开监视段失败 (err=%lu)", GetLastError());
        return;
    }
    void* p = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!p) {
        GP_LOG_DEBUG("monitor: 映射监视段失败 (err=%lu)", GetLastError());
        CloseHandle(h);
        return;
    }
    g_mon = (GpMonBlock*)p;
    g_monMapping = h;

    if (created) {
        memset(g_mon, 0, sizeof(GpMonBlock));
        g_mon->version = GPMON_VERSION;
        g_mon->size    = (uint32_t)sizeof(GpMonBlock);
        MemoryBarrier();
        g_mon->magic = GPMON_MAGIC;
    } else if (g_mon->magic != GPMON_MAGIC ||
               g_mon->version != GPMON_VERSION ||
               g_mon->size != (uint32_t)sizeof(GpMonBlock)) {
        

        GP_LOG_INFO("monitor: 监视段版本不匹配（magic=0x%08X ver=%u size=%u），已断开",
                    g_mon->magic, g_mon->version, g_mon->size);
        UnmapViewOfFile(g_mon);
        CloseHandle(h);
        g_mon = nullptr;
        g_monMapping = nullptr;
        return;
    }
    GP_LOG_INFO("monitor: 监视段就绪（%s）", created ? "本进程创建" : "接手已有");
}

void MonitorShutdown(void) {
    if (g_mon) { UnmapViewOfFile(g_mon); g_mon = nullptr; }
    if (g_monMapping) { CloseHandle(g_monMapping); g_monMapping = nullptr; }
}


int  FourMotorChannel(void);
const char* ChannelName(int ch);
BOOL SendOut(uint32_t controller, const GpFrame* f);
static BOOL SendOutInner(uint32_t controller, const GpFrame* f);

void MonitorPublish(uint32_t c, DWORD now, const MonSample* s) {
    if (!g_mon || !s || c >= GPMON_MAX_CONTROLLERS) return;

    GpMonController* m = &g_mon->ctl[c];
    m->seq++;                 
    MemoryBarrier();

    



    if (m->outLM != s->outLM || m->outRM != s->outRM ||
        m->outLT != s->outLT || m->outRT != s->outRT) {
        m->outCount++;
    }

    m->tickMs  = now;
    m->gameLM  = s->gameLM;  m->gameRM  = s->gameRM;
    m->gameLT  = s->gameLT;  m->gameRT  = s->gameRT;
    m->outLM   = s->outLM;   m->outRM   = s->outRM;
    m->outLT   = s->outLT;   m->outRT   = s->outRT;
    m->padLT   = s->padLT;   m->padRT   = s->padRT;
    m->hasGame = s->hasGame ? 1 : 0;
    m->padValid = s->padValid ? 1 : 0;

    GpHapticsStatus st;
    memset(&st, 0, sizeof(st));
    GpHapticsGetStatus(c, now, &st);
    m->stateValid   = st.stateValid;
    m->aiming       = st.aiming;
    m->menuActive   = st.menuActive;
    m->armed        = st.armed;
    m->weaponGroup  = st.weaponGroup;
    m->aimActive    = st.aimActive;
    m->bowActive    = st.bowActive;
    m->shotActive   = st.shotActive;
    m->shotRemainMs = st.shotRemainMs;
    m->rideActive   = st.rideActive;
    m->ridePeriodMs = st.ridePeriodMs;
    m->rideAmp      = st.rideAmp;
    MemoryBarrier();
    m->seq++;                 

    g_mon->writerHeartbeat = now;
    g_mon->writerPid  = GetCurrentProcessId();
    g_mon->mode       = (uint32_t)g_cfg.mode;
    g_mon->hidCount   = (uint32_t)gphid::Count();
    g_mon->outChannel = (uint32_t)FourMotorChannel();
}









BOOL LaunchTool(const wchar_t* exeName, const char* label) {
    wchar_t exe[MAX_PATH] = {0};

    if (wcschr(exeName, L'\\')) {
        wcsncpy_s(exe, MAX_PATH, exeName, _TRUNCATE);
    } else {
        wchar_t dir[MAX_PATH] = {0};
        if (g_cfg.debugToolDir[0]) {
            wcsncpy_s(dir, MAX_PATH, g_cfg.debugToolDir, _TRUNCATE);
        } else {
            wcsncpy_s(dir, MAX_PATH, gpreal::SelfPath(), _TRUNCATE);
            wchar_t* slash = wcsrchr(dir, L'\\');
            if (!slash) return FALSE;
            slash[1] = 0;
        }
        size_t n = wcslen(dir);
        if (n && dir[n - 1] != L'\\') wcsncat_s(dir, MAX_PATH, L"\\", _TRUNCATE);
        wcsncpy_s(exe, MAX_PATH, dir, _TRUNCATE);
        wcsncat_s(exe, MAX_PATH, exeName, _TRUNCATE);
    }

    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        GP_LOG_INFO("%s: 不在位（%s），跳过", label, gplog::W(exe));
        return FALSE;
    }

    wchar_t cmd[MAX_PATH + 8];
    wcsncpy_s(cmd, MAX_PATH + 8, L"\"", _TRUNCATE);
    wcsncat_s(cmd, MAX_PATH + 8, exe, _TRUNCATE);
    wcsncat_s(cmd, MAX_PATH + 8, L"\"", _TRUNCATE);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                       nullptr, nullptr, &si, &pi)) {
        GP_LOG_INFO("%s: 已启动 pid=%lu（%s）", label, pi.dwProcessId, gplog::W(exe));
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TRUE;
    }
    GP_LOG_INFO("%s: 启动失败 (err=%lu)", label, GetLastError());
    return FALSE;
}

void MonitorMaybeAutoStart(void) {
    if (!g_cfg.monitorAutoStart) return;
    LaunchTool(g_cfg.monitorExe[0] ? g_cfg.monitorExe : L"gp_monitor.exe", "monitor");
}





void DebugMaybeAutoStart(void) {
    if (!g_cfg.debugMode) return;
    GP_LOG_INFO("debug: 调试模式开启（日志级别=%d）—— 拉起排查窗口", g_cfg.logLevel);
    if (g_cfg.debugMonitor)   LaunchTool(L"gp_monitor.exe",    "debug/monitor");
    if (g_cfg.debugStateView) LaunchTool(L"gp_state_view.exe", "debug/state");
}


void BuildFrame(GpFrame* f, uint32_t controller, BYTE lMotor, BYTE rMotor,
                BYTE lTrig, BYTE rTrig, GpSource source, BYTE conn) {
    memset(f, 0, sizeof(*f));
    f->tickMs        = GetTickCount();
    f->pid           = GetCurrentProcessId();
    f->controller    = (uint8_t)controller;
    f->source        = (uint8_t)source;
    f->conn          = conn;
    f->flags         = GP_FRAME_F_FROM_GAME;
    f->rawLeftMotor  = lMotor;
    f->rawRightMotor = rMotor;
    f->rawLeftTrigger  = lTrig;
    f->rawRightTrigger = rTrig;
    f->leftMotor     = lMotor / 255.0f;
    f->rightMotor    = rMotor / 255.0f;
    f->leftTrigger   = lTrig / 255.0f;
    f->rightTrigger  = rTrig / 255.0f;
}




uint64_t PublishDeduped(ControllerState* cs, GpFrame* f) {
    DWORD now = f->tickMs;
    if (cs->dedupValid && cs->lastResultValid &&
        gpipc_same_payload(&cs->lastPublished, f) &&
        (DWORD)(now - cs->lastDedupTick) < kDedupWindowMs) {
        return 0;
    }

    uint64_t seq = gpshm::Publish(f);
    cs->lastPublished = *f;
    cs->lastDedupTick = now;
    cs->dedupValid = TRUE;
    InterlockedIncrement(&g_statCaptured);
    return seq;
}



bool AskProcessor(uint32_t controller, const GpFrame* in, GpFrame* out) {
    DWORD timeout = gpshm::ResultTimeoutMs();
    if (timeout == 0) return false;

    

    if (gpshm::ReadResult(controller, in->seq, out)) return true;

    

    DWORD deadline = GetTickCount() + timeout;
    for (;;) {
        DWORD remain = timeout;
        DWORD now = GetTickCount();
        if ((int32_t)(deadline - now) <= 0) break;
        remain = deadline - now;

        if (!gpshm::WaitResult(remain)) break;
        if (gpshm::ReadResult(controller, in->seq, out)) return true;
    }

    BumpTimeout();
    return false;
}










BOOL WgiAvailable(void) { return gpwgi::Count() > 0; }















int FourMotorChannel(void) {
    if (g_cfg.output == 1) return 0;                       
    if (g_cfg.output == 2) return gphid::Count() > 0 ? 2 : 0;
    if (g_cfg.output == 3) return WgiAvailable() ? 3 : 0;
    if (g_cfg.output == 4) return gphid::IoctlCount() > 0 ? 4 : 0;

    

    if (gphid::IoctlCount() > 0) return 4;
    return WgiAvailable() ? 1 : 0;
}

const char* ChannelName(int ch) {
    switch (ch) {
    case 1:  return "XInput体感 + Windows.Gaming.Input扳机（官方双通道）";
    case 2:  return "HID 报告(四电机，显式选择)";
    case 3:  return "Windows.Gaming.Input(四电机，仅它)";
    case 4:  return "XInput体感 + 直写驱动IOCTL扳机（Xbox 协议）";
    default: return "XInput(只有两个马达，扳机折算进体感)";
    }
}




bool Emit(uint32_t controller, const GpFrame* f, BOOL preferHid) {
    if (preferHid && gphid::Count() > 0) {
        if (gphid::SendMapped((int)controller,
                              f->rawLeftMotor, f->rawRightMotor,
                              f->rawLeftTrigger, f->rawRightTrigger,
                              PulseMode())) {
            InterlockedIncrement(&g_statHidSent);
            return true;
        }
        
        GP_LOG_DEBUG("engine: HID 输出失败，本次退回 XInput");
    }
    return false;
}

void MaybeSendXInput(uint32_t controller, const GpFrame* f) {
    GpFnSetState fn = gpreal::SetStateCall();
    if (!fn) return;
    GpXInputVibration vib;
    vib.wLeftMotorSpeed  = FromRaw(f->rawLeftMotor);
    vib.wRightMotorSpeed = FromRaw(f->rawRightMotor);
    fn(controller, &vib);
}


BOOL SendOut(uint32_t controller, const GpFrame* f) {
    

    t_selfOutput = 1;
    BOOL rc = SendOutInner(controller, f);
    t_selfOutput = 0;
    return rc;
}

static BOOL SendOutInner(uint32_t controller, const GpFrame* f) {
    int ch = FourMotorChannel();
    BYTE lt = g_cfg.triggers ? f->rawLeftTrigger  : 0;
    BYTE rt = g_cfg.triggers ? f->rawRightTrigger : 0;

    if (ch == 3) {
        
        return gpwgi::Set(f->rawLeftMotor, f->rawRightMotor, lt, rt);
    }
    if (ch == 2 && Emit(controller, f, TRUE)) return TRUE;

    if (ch == 4) {
        





        if (!gphid::SendIoctlAll(f->rawLeftMotor, f->rawRightMotor, lt, rt, PulseMode())) {
            

            if (WgiAvailable()) gpwgi::Set(f->rawLeftMotor, f->rawRightMotor, lt, rt);
        }
    } else if (ch == 1) {
        




        gpwgi::Set(f->rawLeftMotor, f->rawRightMotor, lt, rt);
    }
    MaybeSendXInput(controller, f);
    return TRUE;
}













void OutputLoop(void);
DWORD WINAPI OutputThreadProc(LPVOID) {
    g_outputThreadId = GetCurrentThreadId();
    OutputLoop();
    return 0;
}

void OutputLoop(void) {
    

    DWORD lastStats = GetTickCount();

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) break;

        DWORD hz = (DWORD)g_cfg.outputRateHz;
        if (hz < 30) hz = 30;
        DWORD period = 1000 / hz;
        if (period == 0) period = 1;

        

        gpwgi::Tick();
        {
            static int lastCh = -1;
            int ch = FourMotorChannel();
            if (ch != lastCh) {
                lastCh = ch;
                

                const char* bodyCh = "XInput(官方，全手柄通吃)";
                const char* trigCh = "无出口 → 按 TrigToBody=%.2f 折算进体感";
                switch (ch) {
                case 1: bodyCh = "XInput(官方，全手柄通吃)";
                        trigCh = "Windows.Gaming.Input(官方四电机)"; break;
                case 2: bodyCh = "HID 原始报告";
                        trigCh = "HID 原始报告"; break;
                case 3: bodyCh = "Windows.Gaming.Input";
                        trigCh = "Windows.Gaming.Input"; break;
                case 4: bodyCh = "XInput(官方，全手柄通吃)";
                        trigCh = "直写驱动 IOCTL（Xbox 协议）"; break;
                default: break;
                }
                if (ch == 0) {
                    GP_LOG_INFO("engine: 体感通道=%s ｜ 扳机通道=无出口 → "
                                "按 TrigToBody=%.2f 折算进体感（%s）",
                                bodyCh, (double)g_cfg.haptics.trigToBody, ChannelName(ch));
                } else {
                    GP_LOG_INFO("engine: 体感通道=%s ｜ 扳机通道=%s（%s）",
                                bodyCh, trigCh, ChannelName(ch));
                }
            }
        }

        gpshm::BeatProducer();

        GpPolicy policy = ActivePolicy();
        GpControl* ctl = gpshm::Control();

        if (policy == GP_POLICY_REPLACE && ctl) {
            DWORD now = GetTickCount();
            for (uint32_t c = 0; c < GPIPC_MAX_CONTROLLERS; ++c) {
                ControllerState* cs = &g_ctrl[c];

                GpFrame chosen;
                BOOL have = FALSE;

                


                BYTE padLT = 0, padRT = 0;
                if (g_cfg.haptics.enable || g_mon) {
                    ReadPadTriggers(c, &padLT, &padRT);
                    if (g_cfg.haptics.enable) GpOnPadInput(c, now, padLT, padRT);
                }

                

                if (g_cfg.haptics.enable) {
                    GpRdr2State st;
                    BOOL ok = gpgame::Read(&st);
                    


                    if (c == 0) GpOnGameState(c, now, ok, ok ? &st : nullptr);
                    else        GpOnGameState(c, now, FALSE, nullptr);
                }

                GpFrame res;
                if (gpipc_result_read(ctl, c, &res) &&
                    (DWORD)(now - res.tickMs) < kResultStaleMs) {
                    chosen = res;
                    have = TRUE;
                } else if (cs->sawGame && (DWORD)(now - cs->lastGameTick) < kResultStaleMs) {
                    

                    chosen = cs->lastGame;
                    chosen.flags |= GP_FRAME_F_FALLBACK;
                    have = TRUE;
                }

                






                if (g_cfg.haptics.enable) {
                    BYTE baseTL = have ? chosen.rawLeftTrigger  : 0;
                    BYTE baseTR = have ? chosen.rawRightTrigger : 0;

                    GpHapticsOut h;
                    GpTickHaptics(c, now, have,
                                  have ? chosen.rawLeftMotor  : 0,
                                  have ? chosen.rawRightMotor : 0,
                                  baseTL, baseTR, &h);

                    



                    if (FourMotorChannel() == 0 && g_cfg.haptics.trigToBody > 0.0f) {
                        float fold = g_cfg.haptics.trigToBody;
                        

                        float lm = (float)h.leftMotor;
                        float rm = (float)h.rightMotor;
                        if (g_cfg.haptics.driveLeftMotor)  lm += (float)h.leftTrigger  * fold;
                        if (g_cfg.haptics.driveRightMotor) rm += (float)h.rightTrigger * fold;
                        h.leftMotor  = ClampToByte(lm);
                        h.rightMotor = ClampToByte(rm);
                    }

                    


                    if (have || GpHapticsActive(c)) {
                        chosen.controller      = (uint8_t)c;
                        chosen.rawLeftMotor    = h.leftMotor;
                        chosen.rawRightMotor   = h.rightMotor;
                        chosen.rawLeftTrigger  = h.leftTrigger;
                        chosen.rawRightTrigger = h.rightTrigger;
                        chosen.leftMotor     = h.leftMotor / 255.0f;
                        chosen.rightMotor    = h.rightMotor / 255.0f;
                        chosen.leftTrigger   = h.leftTrigger / 255.0f;
                        chosen.rightTrigger  = h.rightTrigger / 255.0f;
                        have = TRUE;
                    } else {
                        have = FALSE;
                    }
                }

                if (!have) {
                    








                    BOOL notSilent = cs->lastSent.rawLeftMotor || cs->lastSent.rawRightMotor ||
                                     cs->lastSent.rawLeftTrigger || cs->lastSent.rawRightTrigger;
                    if (cs->sent && notSilent) {
                        GpFrame zero;
                        memset(&zero, 0, sizeof(zero));
                        zero.controller = (uint8_t)c;
                        zero.tickMs = now;
                        SendOut(c, &zero);
                        cs->lastSent = zero;
                        cs->lastSendTick = now;
                        InterlockedIncrement(&g_statSilenced);
                        GP_LOG_DEBUG("engine: 无依据输出，已发零帧刹车（控制器 %u）", c);
                    }

                    
                    if (g_mon) {
                        MonSample ms;
                        memset(&ms, 0, sizeof(ms));
                        ms.hasGame  = cs->sawGame;
                        ms.padValid = TRUE;
                        ms.padLT = padLT;
                        ms.padRT = padRT;
                        ms.gameLM = cs->lastGame.rawLeftMotor;
                        ms.gameRM = cs->lastGame.rawRightMotor;
                        MonitorPublish(c, now, &ms);
                    }
                    continue;
                }

                



                if (g_mon) {
                    MonSample ms;
                    memset(&ms, 0, sizeof(ms));
                    ms.hasGame  = cs->sawGame;
                    ms.padValid = TRUE;
                    ms.padLT = padLT;
                    ms.padRT = padRT;
                    ms.gameLM = cs->lastGame.rawLeftMotor;
                    ms.gameRM = cs->lastGame.rawRightMotor;
                    ms.outLM = chosen.rawLeftMotor;
                    ms.outRM = chosen.rawRightMotor;
                    ms.outLT = chosen.rawLeftTrigger;
                    ms.outRT = chosen.rawRightTrigger;
                    MonitorPublish(c, now, &ms);
                }

                BOOL changed = !cs->sent || !gpipc_same_payload(&cs->lastSent, &chosen);
                BOOL keepAlive = !cs->sent ||
                                 (DWORD)(now - cs->lastSendTick) >= kKeepAliveMs;
                if (!changed && !keepAlive) continue;

                if (SendOut(c, &chosen)) {
                    cs->lastSent = chosen;
                    cs->lastSendTick = now;
                    cs->sent = TRUE;
                }
            }
        }

        

        DWORD now = GetTickCount();

        

        gpcapture::Tick();

        if ((DWORD)(now - lastStats) >= 5000) {
            lastStats = now;
            GpPolicy p = ActivePolicy();
            static const char* kPolicyName[] = {"透传", "改写", "阻断替换"};
            

            GP_LOG_INFO("engine: 模式=%s 加工端=%s | 捕获=%ld 转发=%ld 吞掉=%ld "
                        "超时=%ld HID发送=%ld 刹车=%ld 环形缓冲=%u 已处理=%u "
                        "| 发出 L=%u R=%u 扳机=%u/%u",
                        kPolicyName[p], gpshm::ConsumerAlive() ? "在线" : "离线(fail-safe)",
                        g_statCaptured, g_statForward, g_statBlocked, g_statTimeouts,
                        g_statHidSent, g_statSilenced,
                        ctl ? ctl->statRingDrops : 0u,
                        ctl ? ctl->statProcessed : 0u,
                        (unsigned)g_ctrl[0].lastSent.rawLeftMotor,
                        (unsigned)g_ctrl[0].lastSent.rawRightMotor,
                        (unsigned)g_ctrl[0].lastSent.rawLeftTrigger,
                        (unsigned)g_ctrl[0].lastSent.rawRightTrigger);
        }

        WaitForSingleObject(g_stopEvent, period);
    }
}













HANDLE g_dumpFile = INVALID_HANDLE_VALUE;
DWORD  g_dumpCount = 0;

void DumpIoctl(DWORD code, LPVOID buf, DWORD size) {
    if (!g_cfg.ioctlDumpPath[0]) return;

    if (g_dumpFile == INVALID_HANDLE_VALUE) {
        g_dumpFile = CreateFileW(g_cfg.ioctlDumpPath, FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_dumpFile == INVALID_HANDLE_VALUE) {
            GP_LOG_ERROR("engine: 打不开 IOCTL 转储文件，转储已关闭");
            g_cfg.ioctlDumpPath[0] = 0;
            return;
        }
        const char* hdr =
            "# 震动 IOCTL 原始缓冲区转储\n"
            "# tick_ms, ioctl, inSize, hex...\n";
        DWORD w = 0;
        WriteFile(g_dumpFile, hdr, (DWORD)strlen(hdr), &w, nullptr);
        GP_LOG_INFO("engine: 开始转储震动 IOCTL 到 %s", gplog::W(g_cfg.ioctlDumpPath));
    }

    

    DWORD n = size > 256 ? 256 : size;
    const BYTE* b = (const BYTE*)buf;

    char line[256 * 3 + 128];
    int off = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "%lu, 0x%08X, %lu,", GetTickCount(), code, size);
    if (off < 0) return;
    for (DWORD i = 0; i < n && off < (int)sizeof(line) - 4; ++i) {
        off += _snprintf_s(line + off, sizeof(line) - (size_t)off, _TRUNCATE,
                           " %02X", b[i]);
        if (off < 0) break;
    }
    if (off > 0) {
        line[off++] = '\r';
        line[off++] = '\n';
        DWORD w = 0;
        WriteFile(g_dumpFile, line, (DWORD)off, &w, nullptr);
        ++g_dumpCount;
    }
}

void DumpIoctlClose(void) {
    if (g_dumpFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dumpFile);
        g_dumpFile = INVALID_HANDLE_VALUE;
        GP_LOG_INFO("engine: IOCTL 转储结束，共 %lu 条", g_dumpCount);
    }
}



bool RangeAccessible(const void* ptr, SIZE_T size, BOOL needWrite) {
    if (!ptr || size == 0) return false;

    const BYTE* p = (const BYTE*)ptr;
    const BYTE* end = p + size;

    while (p < end) {
        MEMORY_BASIC_INFORMATION mbi = {0};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

        DWORD prot = mbi.Protect & 0xFF;
        BOOL writable = (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                         prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
        BOOL readable = writable || prot == PAGE_READONLY || prot == PAGE_EXECUTE_READ;
        if (!readable) return false;
        if (needWrite && !writable) return false;

        const BYTE* regionEnd = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
        if (regionEnd <= p) return false;   
        p = regionEnd;
    }
    return true;
}

}  

void Start(void) {
    GP_LOG_DEBUG("engine: 读取配置");
    GpConfigLoad(&g_cfg);

    








    {
        wchar_t once_name[80];
        _snwprintf_s(once_name, _TRUNCATE, L"Local\\GpXInputProxy_v1_%lu",
                     (unsigned long)GetCurrentProcessId());
        HANDLE once = CreateMutexW(nullptr, TRUE, once_name);
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
            g_secondary = TRUE;
            GP_LOG_INFO("engine: 本进程里已经有一个代理实例在跑 —— 本实例只转发，"
                        "不合成、不输出、不挂钩（两个实例同时输出会乱震）");
            return;
        }
        
    }

    gplog::SetLevel((gplog::Level)g_cfg.logLevel);

    

    gpcapture::Init(g_cfg.capturePath);

    

    GpApplyHapticsSettings(g_cfg.haptics);

    MonitorInit();

    

    gpreal::SetForceFfb(g_cfg.forceFfbCapability != FALSE);
    if (g_cfg.forceFfbCapability) {
        GP_LOG_INFO("engine: 已开启 ForceFFBCapability —— 会向游戏报告"
                    "「本手柄支持震动」。若游戏因此开始发震动，说明之前抓不到"
                    "只是因为设备没声明这一位。");
    }
    GP_LOG_DEBUG("engine: 配置已读 mode=%d output=%d triggers=%d logLevel=%d",
                 g_cfg.mode, g_cfg.output, g_cfg.triggers, g_cfg.logLevel);

    GP_LOG_DEBUG("engine: 连接共享内存");
    if (!gpshm::Attach()) {
        GP_LOG_ERROR("engine: 共享内存连接失败 —— 拦截功能不可用，"
                     "所有调用将原样透传给真身");
    }

    GP_LOG_DEBUG("engine: 枚举 HID 设备");
    gphid::SetBluetoothAllowed(g_cfg.hidAllowBluetooth);
    gphid::SetVendorFilter(g_cfg.hidVendorIds, g_cfg.hidVendorIdCount,
                          g_cfg.hidAnyGamepad);
    gphid::Init();
    GP_LOG_DEBUG("engine: HID 枚举完成，%d 个设备", gphid::Count());

    

    if (g_cfg.haptics.enable && g_cfg.mode != 2) {
        GP_LOG_INFO("engine: 当前模式=%d，扳机需要 Mode=2（阻断替换）才有输出通道 —— "
                    "其余模式只能驱动两个体感马达", g_cfg.mode);
    }

    memset(g_ctrl, 0, sizeof(g_ctrl));

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent) {
        InterlockedExchange(&g_running, 1);
        g_outputThread = CreateThread(nullptr, 0, OutputThreadProc, nullptr, 0, nullptr);
        GP_LOG_DEBUG("engine: 输出线程已创建 = %p", g_outputThread);
    }

    static const char* kPolicyName[] = {"透传", "改写", "阻断替换"};
    GP_LOG_INFO("engine: 就绪 模式=%s 输出配置=%d(%s) 扳机=%d 超时=%dms 输出频率=%dHz",
                kPolicyName[g_cfg.mode], g_cfg.output,
                g_cfg.output == 0 ? "自动：XInput体感+WGI扳机" :
                g_cfg.output == 1 ? "只用XInput" :
                g_cfg.output == 2 ? "只用HID原始报告" : "只用WGI",
                g_cfg.triggers, g_cfg.timeoutMs, g_cfg.outputRateHz);
    if (g_cfg.mode == 0) {
        GP_LOG_INFO("engine: 当前是透传模式，只观察不干预。"
                    "要真的接管震动，请在同目录 gpxinput.ini 里把 Mode 改成 1(改写) 或 2(阻断替换)");
    }

    

    gpgame::Attach();
    MonitorMaybeAutoStart();
    DebugMaybeAutoStart();
}

void Stop(void) {
    if (InterlockedExchange(&g_running, 0)) {
        if (g_stopEvent) SetEvent(g_stopEvent);
        if (g_outputThread) {
            WaitForSingleObject(g_outputThread, 1000);
            CloseHandle(g_outputThread);
            g_outputThread = nullptr;
        }
    }
    if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = nullptr; }
    DumpIoctlClose();
    gpcapture::Shutdown();
    GpResetHaptics();
    MonitorShutdown();
    gpgame::Detach();
    gphid::Shutdown();
    gpshm::Detach();
}


BOOL IsKnownIoctl(DWORD code) {
    switch (code) {
    case 0x8000a010:            
    case 0x002aac08:            
    case 0xb0000:               
    case 0xb0001:               
    case 0xb0002:               
    case 0xb0003:               
    case 0xb0004:               
    case 0xb0011: case 0xb0012: case 0xb0013: case 0xb0014:
    case 0xb0019:               
        return TRUE;
    default: return FALSE;
    }
}

bool IsVibrationIoctl(DWORD code) {
    switch (code) {
    case 0x002aac08:   
    case 0x8000a010:   
    case 0x8000a008:   
        return true;
    default:
        break;
    }
    for (int i = 0; i < g_cfg.extraIoctlCount; ++i) {
        if (g_cfg.extraIoctls[i] == code) return true;
    }
    return false;
}

int PulseMode(void) {
    return g_cfg.pulse;
}

DWORD OnSetState(DWORD controller, WORD left, WORD right, GpFnSetState downstream) {
    
    GpFrame in;
    BYTE conn = GP_CONN_USB_24G;
    if (gphid::Count() > 0 && gphid::IsBluetooth((int)(controller % (uint32_t)gphid::Count()))) {
        conn = GP_CONN_BLUETOOTH;
    }
    BuildFrame(&in, controller, ToRaw(left), ToRaw(right), 0, 0, GP_SRC_XINPUT, conn);

    

    if (left == 0 && right == 0) in.flags |= GP_FRAME_F_FULL_TICK;

    




    GpOnGameFrame(controller, in.tickMs, in.rawLeftMotor, in.rawRightMotor);

    










    {
        static volatile LONG s_total = 0, s_window = 0;
        static DWORD s_lastSummary = 0, s_lastChange = 0;
        static BYTE  s_lastRaw[4] = {0, 0, 0, 0};
        static BOOL  s_sawFirst = FALSE;

        InterlockedIncrement(&s_total);
        InterlockedIncrement(&s_window);

        DWORD nowMs = GetTickCount();
        BYTE raw[4] = { in.rawLeftMotor, in.rawRightMotor,
                        in.rawLeftTrigger, in.rawRightTrigger };

        if (!s_sawFirst) {
            s_sawFirst = TRUE;
            GP_LOG_INFO("engine: **收到第一帧 XInput 震动** —— 这条通道通了 "
                        "(LM=%u RM=%u LT=%u RT=%u)",
                        raw[0], raw[1], raw[2], raw[3]);
        }

        if (memcmp(raw, s_lastRaw, 4) != 0 && (DWORD)(nowMs - s_lastChange) >= 200) {
            s_lastChange = nowMs;
            memcpy(s_lastRaw, raw, 4);
            if (raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0) {
                GP_LOG_INFO("engine: 震动停止（全零）");
            } else {
                GP_LOG_INFO("engine: 震动 LM=%u RM=%u LT=%u RT=%u",
                            raw[0], raw[1], raw[2], raw[3]);
            }
        }

        if ((DWORD)(nowMs - s_lastSummary) >= 5000) {
            s_lastSummary = nowMs;
            LONG w = InterlockedExchange(&s_window, 0);
            GP_LOG_INFO("engine: 小计 —— 最近 5 秒 %ld 帧，累计 %ld 帧",
                        w, s_total);
        }
    }

    uint64_t publishedSeq = 0;
    ControllerState* cs = nullptr;
    if (controller < GPIPC_MAX_CONTROLLERS) cs = &g_ctrl[controller];

    if (controller < GPIPC_MAX_CONTROLLERS) {
        cs->lastGame = in;
        cs->lastGameTick = in.tickMs;
        cs->sawGame = TRUE;
        if (!cs->xinputTookOver) {
            cs->xinputTookOver = TRUE;
            GP_LOG_INFO("engine: 检测到本进程通过 XInput 发震动，"
                        "DeviceIoControl 路径降级为只观察（它读到的是同一次震动的"
                        "底层镜像，且布局与本项目假定不符，采信它会改坏震动）");
        }
        publishedSeq = PublishDeduped(cs, &in);
    } else {
        publishedSeq = gpshm::Publish(&in);
        InterlockedIncrement(&g_statCaptured);
    }

    








    if (gpcapture::Enabled()) {
        BYTE padLT = 0, padRT = 0;
        ReadPadTriggers(controller, &padLT, &padRT);
        




        const GpFrame* outFrame = (cs && cs->sent) ? &cs->lastSent : &in;
        gpcapture::Frame(&in, outFrame, padLT, padRT);
    }

    GpPolicy policy = ActivePolicy();

    if (g_secondary) policy = GP_POLICY_PASSTHROUGH;   
    if (policy == GP_POLICY_PASSTHROUGH || !downstream) {
        

        BumpForward();
        if (!downstream) return ERROR_SUCCESS;
        GpXInputVibration vib;
        vib.wLeftMotorSpeed = left;
        vib.wRightMotorSpeed = right;
        return downstream(controller, &vib);
    }

    





    GpFrame out = in;
    BOOL haveResult = FALSE;

    





    BOOL askProcessor = !(g_cfg.haptics.enable && !gpshm::ConsumerAlive());

    if (publishedSeq == 0 && cs && cs->lastResultValid && askProcessor) {
        out = cs->lastResult;
        out.flags |= GP_FRAME_F_PROCESSED;
        haveResult = TRUE;
    } else if (askProcessor && AskProcessor(controller, &in, &out)) {
        out.flags |= GP_FRAME_F_PROCESSED;
        haveResult = TRUE;
        if (cs) {
            cs->lastResult = out;
            cs->lastResultValid = TRUE;
        }
    }

    if (!haveResult) {
        out = in;
        out.flags |= GP_FRAME_F_FALLBACK;
    }

    if (policy == GP_POLICY_REPLACE) {
        
        BumpBlocked();
        return ERROR_SUCCESS;
    }

    




    if (g_cfg.haptics.enable) {
        GpHapticsOut h;
        GpTickHaptics(controller, GetTickCount(), TRUE,
                      out.rawLeftMotor, out.rawRightMotor,
                      out.rawLeftTrigger, out.rawRightTrigger, &h);

        if (FourMotorChannel() == 0 && g_cfg.haptics.trigToBody > 0.0f) {
            float fold = g_cfg.haptics.trigToBody;
            h.leftMotor  = ClampToByte((float)h.leftMotor  + (float)h.leftTrigger  * fold);
            h.rightMotor = ClampToByte((float)h.rightMotor + (float)h.rightTrigger * fold);
        }

        out.rawLeftMotor    = h.leftMotor;
        out.rawRightMotor   = h.rightMotor;
        out.rawLeftTrigger  = h.leftTrigger;
        out.rawRightTrigger = h.rightTrigger;
        out.leftMotor     = h.leftMotor / 255.0f;
        out.rightMotor    = h.rightMotor / 255.0f;
        out.leftTrigger   = h.leftTrigger / 255.0f;
        out.rightTrigger  = h.rightTrigger / 255.0f;
    }

    


    if (FourMotorChannel() != 0) {
        if (SendOut(controller, &out)) {
            BumpBlocked();
            return ERROR_SUCCESS;
        }
        

    }

    BumpForward();
    if (!downstream) return ERROR_SUCCESS;
    GpXInputVibration vib;
    vib.wLeftMotorSpeed  = FromRaw(out.rawLeftMotor);
    vib.wRightMotorSpeed = FromRaw(out.rawRightMotor);
    return downstream(controller, &vib);
}











bool SecondaryInstance(void) { return g_secondary != FALSE; }



volatile HANDLE g_hookCheckHandle = nullptr;

void SetHookCheckHandle(HANDLE h) { g_hookCheckHandle = h; }

bool ShouldBlockNativeOutput(void) {
    if (ActivePolicy() != GP_POLICY_REPLACE) return false;
    return !IsSelfWrite(nullptr);      
}

bool OnNativeHidWrite(HANDLE hDevice, const void* buffer, DWORD len) {
    if (!gphid::IsOurHandle(hDevice)) return false;
    if (IsSelfWrite(hDevice)) return false;          

    


    if (len >= 6) {
        const BYTE* b = (const BYTE*)buffer;
        GpOnGameFrame(0, GetTickCount(), b[4], b[5]);
        GP_LOG_DEBUG("engine: 采信原生 HID 写 LM=%u RM=%u LT=%u RT=%u（%u 字节）",
                     b[4], b[5], b[2], b[3], len);
    }
    return ShouldBlockNativeOutput();
}

bool IsSelfWrite(HANDLE hDevice) {
    (void)hDevice;
    if (t_selfOutput) return true;                  
    if (g_outputThreadId != 0 && GetCurrentThreadId() == g_outputThreadId) return true;
    return false;
}

bool OnDeviceIoControl(DWORD ioctlCode, LPVOID inBuffer, DWORD inSize) {
    if (!IsVibrationIoctl(ioctlCode)) {
        



        if (gphid::IsOurHandle(g_hookCheckHandle) && !IsKnownIoctl(ioctlCode)) {
            static DWORD seen[16] = {0};
            static int   seenN = 0;
            BOOL dup = FALSE;
            for (int i = 0; i < seenN; ++i) if (seen[i] == ioctlCode) { dup = TRUE; break; }
            if (!dup) {
                if (seenN < 16) seen[seenN++] = ioctlCode;
                GP_LOG_INFO("engine: 发往手柄设备的未识别 IOCTL 0x%08X（%u 字节）"
                            "—— 若手柄仍有原生震动残留，就是这条通道，请把这行发我",
                            ioctlCode, inSize);
            }
        }
        return false;
    }

    

    if (!RangeAccessible(inBuffer, inSize, FALSE)) {
        GP_LOG_DEBUG("engine: IOCTL 0x%08X 的输入缓冲区不可读，放弃拦截", ioctlCode);
        return false;
    }

    BYTE lMotor = 0, rMotor = 0, lTrig = 0, rTrig = 0;
    BOOL parsed = FALSE;

    

    if (ioctlCode == 0x002aac08 && inSize >= 7) {
        





        DWORD off = inSize - 7;
        const BYTE* b = (const BYTE*)inBuffer;
        lTrig = b[off + 0];
        rTrig = b[off + 1];
        lMotor = b[off + 2];
        rMotor = b[off + 3];
        parsed = TRUE;
    } else if (ioctlCode == 0x8000a010 && inSize >= 4) {
        













        const BYTE* b = (const BYTE*)inBuffer;
        lMotor = b[2];
        rMotor = b[3];
        parsed = TRUE;
    }

    if (!parsed) return false;

    GpSource src = (ioctlCode == 0x002aac08) ? GP_SRC_STEAM_IOCTL : GP_SRC_MS_IOCTL;

    








    



    DumpIoctl(ioctlCode, inBuffer, inSize);

    ControllerState* cs = &g_ctrl[0];
    if (cs->xinputTookOver) {
        if (!cs->xinputTakeoverLogged) {
            cs->xinputTakeoverLogged = TRUE;
            GP_LOG_INFO("engine: 开始忽略 IOCTL 0x%08X 的震动数据"
                        "（本进程已在用 XInput，那才是权威来源）", ioctlCode);
        }

        


        return gpshm::EffectivePolicy() == GP_POLICY_REPLACE;
    }

    GpFrame in;
    BuildFrame(&in, 0, lMotor, rMotor, lTrig, rTrig, src, GP_CONN_USB_24G);
    if (lMotor == 0 && rMotor == 0 && lTrig == 0 && rTrig == 0) {
        in.flags |= GP_FRAME_F_FULL_TICK;
    }

    GP_LOG_DEBUG("engine: 捕获 IOCTL 0x%08X 左马达=%u 右马达=%u 左扳机=%u 右扳机=%u"
                 "（缓冲区 %u 字节）",
                 ioctlCode, lMotor, rMotor, lTrig, rTrig, inSize);

    uint64_t publishedSeq = PublishDeduped(cs, &in);

    GpPolicy policy = ActivePolicy();
    if (policy == GP_POLICY_PASSTHROUGH) return false;

    GpFrame out = in;
    BOOL haveResult = FALSE;
    if (publishedSeq == 0 && cs->lastResultValid) {
        
        out = cs->lastResult;
        out.flags |= GP_FRAME_F_PROCESSED;
        haveResult = TRUE;
    } else if (AskProcessor(0, &in, &out)) {
        out.flags |= GP_FRAME_F_PROCESSED;
        haveResult = TRUE;
        cs->lastResult = out;
        cs->lastResultValid = TRUE;
    }
    if (!haveResult) {
        out = in;
        out.flags |= GP_FRAME_F_FALLBACK;
    }

    if (policy == GP_POLICY_REPLACE) {
        BumpBlocked();
        return true;    
    }

    

    if (!g_cfg.ioctlModify) {
        GP_LOG_TRACE("engine: 未开启 IoctlModify，只观察不改写 IOCTL 缓冲区");
        InterlockedIncrement(&g_statForward);
        return false;
    }

    if (!RangeAccessible(inBuffer, inSize, TRUE)) return false;

    BYTE* b = (BYTE*)inBuffer;
    if (ioctlCode == 0x002aac08 && inSize >= 7) {
        DWORD off = inSize - 7;
        b[off + 0] = out.rawLeftTrigger;
        b[off + 1] = out.rawRightTrigger;
        b[off + 2] = out.rawLeftMotor;
        b[off + 3] = out.rawRightMotor;
    } else if (ioctlCode == 0x8000a010 && inSize >= 4) {
        DWORD off = inSize - 4;
        b[off + 2] = out.rawLeftMotor;
        b[off + 3] = out.rawRightMotor;
    }

    BumpForward();
    return false;
}

}  
