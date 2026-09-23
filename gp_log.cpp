// log
#include "gp_log.h"
#include "gp_text.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace gplog {

namespace {

CRITICAL_SECTION g_lock;
volatile LONG     g_lockReady = 0;
HANDLE            g_file      = INVALID_HANDLE_VALUE;
static wchar_t    g_path[MAX_PATH] = {0};
Level             g_level     = LVL_TRACE;   



const int kLineMax = 2048;

void EnsureLock(void) {
    

    if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
    }
}

}  

void Init(const wchar_t* path) {
    EnsureLock();

    wchar_t fallback[MAX_PATH] = {0};
    if (!path) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&Init, &self);
        GetModuleFileNameW(self, fallback, MAX_PATH);
        wchar_t* dot = wcsrchr(fallback, L'.');
        if (dot) *dot = 0;
        
        wchar_t suffix[32];
        _snwprintf_s(suffix, 32, _TRUNCATE, L".%lu.log", GetCurrentProcessId());
        wcscat_s(fallback, MAX_PATH, suffix);
        path = fallback;
    }

    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    wcsncpy_s(g_path, MAX_PATH, path, _TRUNCATE);
    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LeaveCriticalSection(&g_lock);
}

void Shutdown(void) {
    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_lock);
}

const char* PathUtf8(void) {
    static char buf[MAX_PATH * 2] = {0};
    buf[0] = 0;
    WideCharToMultiByte(CP_UTF8, 0, g_path, -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

void SetLevel(Level lvl) { g_level = lvl; }
Level GetLevel(void) { return g_level; }

const char* W(const wchar_t* s) {
    


    const int kSlots = 4;
    const int kSize = 1024;
    static thread_local char bufs[kSlots][kSize];
    static thread_local int next = 0;

    char* b = bufs[next];
    next = (next + 1) % kSlots;
    GpWideToUtf8(s, b, kSize);
    return b;
}

void Write(Level lvl, const char* fmt, ...) {
    if (lvl > g_level || g_level == LVL_OFF) return;
    EnsureLock();

    char body[kLineMax];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[kLineMax + 64];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "[%02u:%02u:%02u.%03u][pid %lu][tid %lu] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentProcessId(), GetCurrentThreadId(), body);

    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_file, line, (DWORD)strlen(line), &written, nullptr);
    }
    LeaveCriticalSection(&g_lock);
}

void HexDump(Level lvl, const char* tag, const void* data, unsigned int len) {
    if (lvl > g_level || g_level == LVL_OFF) return;
    if (!data || len == 0) return;

    const unsigned char* p = (const unsigned char*)data;
    
    unsigned int n = len > 64 ? 64 : len;

    char buf[64 * 3 + 8];
    int off = 0;
    for (unsigned int i = 0; i < n && off < (int)sizeof(buf) - 4; ++i) {
        off += _snprintf_s(buf + off, sizeof(buf) - off, _TRUNCATE, "%02X ", p[i]);
    }
    buf[off] = 0;
    Write(lvl, "%s (%u bytes): %s", tag, len, buf);
}

}  
