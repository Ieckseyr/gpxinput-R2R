// state
#include "gp_gamestate.h"
#include "gp_log.h"

#include <string.h>

namespace gpgame {

namespace {
HANDLE            g_mapping = nullptr;
const GpRdr2State* g_view    = nullptr;
bool              g_everHad  = false;
bool              g_warnedMissing = false;
}

void Attach(void) {
    if (g_view) return;

    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, GPRDR2_NAME);
    if (!g_mapping) {
        

        if (!g_warnedMissing) {
            g_warnedMissing = true;
            GP_LOG_INFO("gamestate: 没有游戏状态段（.asi 未加载？）—— "
                        "按武器区分的功能不可用，其余照常");
        }
        return;
    }

    void* p = MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, 0);
    if (!p) {
        GP_LOG_INFO("gamestate: 映射状态段失败 (err=%lu)", GetLastError());
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return;
    }
    g_view = (const GpRdr2State*)p;

    GP_LOG_INFO("gamestate: 状态段已连接（写入端 pid=%u）", g_view->writerPid);
}

void Detach(void) {
    if (g_view) { UnmapViewOfFile((LPCVOID)g_view); g_view = nullptr; }
    if (g_mapping) { CloseHandle(g_mapping); g_mapping = nullptr; }
}

bool Read(GpRdr2State* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    
    if (!g_view) {
        static DWORD lastTry = 0;
        DWORD now = GetTickCount();
        if (lastTry == 0 || (DWORD)(now - lastTry) > 2000) {
            lastTry = now;
            Attach();
        }
        if (!g_view) return false;
    }

    

    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t s0 = g_view->seq;
        if (s0 & 1u) continue;               
        *out = *g_view;
        MemoryBarrier();
        if (g_view->seq == s0) break;
    }

    


    if (out->magic != GPRDR2_MAGIC || out->version != GPRDR2_VERSION) return false;

    DWORD now = GetTickCount();
    if (out->tickMs == 0 || (DWORD)(now - out->tickMs) > GPRDR2_TIMEOUT_MS) return false;
    if (out->playerPed == 0) return false;

    g_everHad = true;
    return true;
}

bool EverHad(void) { return g_everHad; }

}  
