// capture
#include "gp_capture.h"
#include "gp_log.h"

#include <stdio.h>

namespace gpcapture {



static const size_t kBufSize = 512 * 1024;



static const DWORD kFlushPeriodMs = 1000;

static FILE* g_file      = nullptr;
static char  g_buf[kBufSize];
static size_t g_len      = 0;
static DWORD g_lastFlush = 0;

BOOL Enabled(void) {
    return g_file != nullptr;
}

static void Flush(void) {
    if (!g_file || g_len == 0) return;
    fwrite(g_buf, 1, g_len, g_file);
    fflush(g_file);
    g_len = 0;
    g_lastFlush = GetTickCount();
}

void Init(const wchar_t* path) {
    if (!path || !path[0]) {
        GP_LOG_DEBUG("capture: 未配置 CaptureFile，不录制");
        return;
    }

    

    g_file = _wfopen(path, L"wb");
    if (!g_file) {
        

        GP_LOG_ERROR("capture: 打不开录制文件，已禁用录制: %s", gplog::W(path));
        return;
    }

    



    static const char kHeader[] =
        "seq,tick_ms,controller,source,flags,conn,"
        "in_lmotor,in_rmotor,in_ltrig,in_rtrig,"
        "out_lmotor,out_rmotor,out_ltrig,out_rtrig,"
        "pad_ltrig,pad_rtrig\n";
    fwrite(kHeader, 1, sizeof(kHeader) - 1, g_file);
    fflush(g_file);

    g_len = 0;
    g_lastFlush = GetTickCount();
    GP_LOG_INFO("capture: 开始录制震动帧 -> %s", gplog::W(path));
}

void Frame(const GpFrame* in, const GpFrame* out, BYTE padLT, BYTE padRT) {
    if (!g_file || !in || !out) return;

    


    if (g_len + 256 > kBufSize) Flush();

    int n = snprintf(g_buf + g_len, kBufSize - g_len,
                     "%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                     (unsigned long long)in->seq,
                     (unsigned)in->tickMs,
                     (unsigned)in->controller,
                     (unsigned)in->source,
                     (unsigned)in->flags,
                     (unsigned)in->conn,
                     (unsigned)in->rawLeftMotor,
                     (unsigned)in->rawRightMotor,
                     (unsigned)in->rawLeftTrigger,
                     (unsigned)in->rawRightTrigger,
                     (unsigned)out->rawLeftMotor,
                     (unsigned)out->rawRightMotor,
                     (unsigned)out->rawLeftTrigger,
                     (unsigned)out->rawRightTrigger,
                     (unsigned)padLT,
                     (unsigned)padRT);
    if (n > 0) g_len += (size_t)n;
}

void Tick(void) {
    if (!g_file) return;
    if ((DWORD)(GetTickCount() - g_lastFlush) >= kFlushPeriodMs) Flush();
}

void Shutdown(void) {
    if (!g_file) return;
    Flush();
    fclose(g_file);
    g_file = nullptr;
    g_len = 0;
    GP_LOG_INFO("capture: 录制结束");
}

}  
