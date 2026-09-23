// ipc
#include "gp_ipc_client.h"
#include "gp_log.h"

namespace gpshm {
namespace {

HANDLE     g_hCtl        = nullptr;
HANDLE     g_hRing       = nullptr;
HANDLE     g_hFrameEvt   = nullptr;
HANDLE     g_hResultEvt  = nullptr;
GpControl* g_ctl         = nullptr;
GpRing*    g_ring        = nullptr;
bool       g_attached    = false;
bool       g_isCreator   = false;
uint32_t   g_lastBeat    = 0;



HANDLE OpenSection(const wchar_t* name, SIZE_T size, bool* created, bool* versionBad) {
    *created = false;
    *versionBad = false;

    

    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (h) return h;

    h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                           (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), name);
    if (h && GetLastError() != ERROR_ALREADY_EXISTS) {
        *created = true;
    }
    return h;
}

}  

bool Attach(void) {
    if (g_attached) return true;

    bool createdCtl = false, createdRing = false, badVersion = false;

    g_hCtl = OpenSection(GPIPC_NAME_CTL, sizeof(GpControl), &createdCtl, &badVersion);
    if (!g_hCtl) {
        GP_LOG_ERROR("gpshm: 创建/打开控制块失败 (err=%lu)", GetLastError());
        return false;
    }
    g_ctl = (GpControl*)MapViewOfFile(g_hCtl, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!g_ctl) {
        GP_LOG_ERROR("gpshm: 映射控制块失败 (err=%lu)", GetLastError());
        CloseHandle(g_hCtl); g_hCtl = nullptr;
        return false;
    }

    if (createdCtl) {
        


        memset(g_ctl, 0, sizeof(GpControl));
        g_ctl->version = GPIPC_VERSION;
        g_ctl->size    = sizeof(GpControl);
        g_ctl->policy  = GP_POLICY_PASSTHROUGH;
        g_ctl->output  = GP_OUT_AUTO;
        g_ctl->timeoutMs = 2;
        g_ctl->enabled = 1;
        MemoryBarrier();
        g_ctl->magic = GPIPC_MAGIC;
        g_isCreator = true;
    } else {
        if (g_ctl->magic != GPIPC_MAGIC ||
            g_ctl->version != GPIPC_VERSION ||
            g_ctl->size != sizeof(GpControl)) {
            GP_LOG_ERROR("gpshm: 版本不匹配 magic=0x%08X ver=%u size=%u (期望 0x%08X/%u/%u)"
                         " —— 拒绝连接以避免读到错位的数据",
                         g_ctl->magic, g_ctl->version, g_ctl->size,
                         (unsigned)GPIPC_MAGIC, (unsigned)GPIPC_VERSION,
                         (unsigned)sizeof(GpControl));
            UnmapViewOfFile(g_ctl); g_ctl = nullptr;
            CloseHandle(g_hCtl);    g_hCtl = nullptr;
            return false;
        }
    }

    g_hRing = OpenSection(GPIPC_NAME_RING, sizeof(GpRing), &createdRing, &badVersion);
    if (!g_hRing) {
        GP_LOG_ERROR("gpshm: 创建/打开环形缓冲失败 (err=%lu)", GetLastError());
        return false;
    }
    g_ring = (GpRing*)MapViewOfFile(g_hRing, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!g_ring) {
        GP_LOG_ERROR("gpshm: 映射环形缓冲失败 (err=%lu)", GetLastError());
        return false;
    }
    if (createdRing) {
        memset(g_ring, 0, sizeof(GpRing));
    }

    

    g_hFrameEvt  = CreateEventW(nullptr, FALSE, FALSE, GPIPC_NAME_FRAME_EVT);
    g_hResultEvt = CreateEventW(nullptr, FALSE, FALSE, GPIPC_NAME_RESULT_EVT);
    if (!g_hFrameEvt || !g_hResultEvt) {
        GP_LOG_ERROR("gpshm: 创建事件失败 (err=%lu)", GetLastError());
        return false;
    }

    

    InterlockedIncrement((volatile LONG*)&g_ctl->producerCount);

    g_attached = true;
    GP_LOG_INFO("gpshm: 已连接（%s）ctl=%p ring=%p 已有生产者=%u",
                g_isCreator ? "本进程创建" : "接手已有",
                (void*)g_ctl, (void*)g_ring, g_ctl->producerCount);
    return true;
}

void Detach(void) {
    if (g_ctl) {
        InterlockedDecrement((volatile LONG*)&g_ctl->producerCount);
    }
    if (g_ring)        { UnmapViewOfFile(g_ring); g_ring = nullptr; }
    if (g_ctl)         { UnmapViewOfFile(g_ctl);  g_ctl  = nullptr; }
    if (g_hRing)       { CloseHandle(g_hRing);      g_hRing = nullptr; }
    if (g_hCtl)        { CloseHandle(g_hCtl);       g_hCtl = nullptr; }
    if (g_hFrameEvt)   { CloseHandle(g_hFrameEvt);  g_hFrameEvt = nullptr; }
    if (g_hResultEvt)  { CloseHandle(g_hResultEvt); g_hResultEvt = nullptr; }
    g_attached = false;
}

bool IsAttached(void) { return g_attached; }
GpControl* Control(void) { return g_ctl; }

uint64_t Publish(GpFrame* frame) {
    if (!g_attached || !g_ring || !frame) return 0;
    uint64_t seq = gpipc_ring_push(g_ring, frame);
    InterlockedIncrement((volatile LONG*)&g_ctl->statFramesIn);
    if (g_hFrameEvt) SetEvent(g_hFrameEvt);
    return seq;
}

void BeatProducer(void) {
    if (!g_attached || !g_ctl) return;
    g_ctl->producerHeartbeat = GetTickCount();
}

void Release(void) {
    Detach();
}

bool ConsumerAlive(void) {
    if (!g_attached || !g_ctl) return false;
    return gpipc_consumer_alive(g_ctl, GetTickCount()) != 0;
}

GpPolicy EffectivePolicy(void) {
    if (!g_attached || !g_ctl) return GP_POLICY_PASSTHROUGH;
    

    if (!g_ctl->enabled)    return GP_POLICY_PASSTHROUGH;
    if (!ConsumerAlive())   return GP_POLICY_PASSTHROUGH;
    uint32_t p = g_ctl->policy;
    if (p > GP_POLICY_REPLACE) return GP_POLICY_PASSTHROUGH;
    return (GpPolicy)p;
}

GpOutput PreferredOutput(void) {
    if (!g_attached || !g_ctl) return GP_OUT_AUTO;
    uint32_t o = g_ctl->output;
    if (o > GP_OUT_HID) return GP_OUT_AUTO;
    return (GpOutput)o;
}

DWORD ResultTimeoutMs(void) {
    if (!g_attached || !g_ctl) return 0;
    DWORD t = g_ctl->timeoutMs;
    if (t > 50) t = 50;   
    return t;
}

bool WaitResult(DWORD timeoutMs) {
    if (!g_attached || !g_hResultEvt) return false;
    if (timeoutMs == 0) return false;
    return WaitForSingleObject(g_hResultEvt, timeoutMs) == WAIT_OBJECT_0;
}

bool ReadResult(uint32_t controller, uint64_t frameSeq, GpFrame* out) {
    if (!g_attached || !g_ctl || !out) return false;
    GpFrame f;
    if (!gpipc_result_read(g_ctl, controller, &f)) return false;
    

    if (f.seq != frameSeq) return false;
    *out = f;
    return true;
}

}  
