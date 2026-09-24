










#ifndef GP_DATA_H
#define GP_DATA_H

#include <windows.h>
#include <stdio.h>
#include <string.h>


static __inline BOOL GpExeDir(char* out, int chars) {
    wchar_t wide[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, wide, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { if (chars > 0) out[0] = 0; return FALSE; }

    wchar_t* slash = wcsrchr(wide, L'\\');
    if (slash) *slash = 0;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, chars, nullptr, nullptr) <= 0) {
        out[0] = 0;
        return FALSE;
    }
    return out[0] != 0;
}


static __inline BOOL GpDataDir(char* out, int chars) {
    char exeDir[MAX_PATH];
    if (!GpExeDir(exeDir, sizeof(exeDir))) return FALSE;

    _snprintf_s(out, (size_t)chars, _TRUNCATE, "%s\\data", exeDir);

    wchar_t wide[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, out, -1, wide, MAX_PATH) <= 0) return FALSE;

    if (!CreateDirectoryW(wide, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            

            _snprintf_s(out, (size_t)chars, _TRUNCATE, "%s", exeDir);
            return TRUE;
        }
    }
    return TRUE;
}


static __inline void GpStampFile(char* out, int chars) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, (size_t)chars, _TRUNCATE, "%04u%02u%02u_%02u%02u%02u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}


static __inline void GpStampFull(char* out, int chars) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, (size_t)chars, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}


static __inline void GpStampClock(char* out, int chars) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, (size_t)chars, _TRUNCATE, "%02u:%02u:%02u.%03u",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}



static __inline unsigned long long GpNowMs(void) {
    static LARGE_INTEGER freq;
    static int ready = 0;
    if (!ready) { QueryPerformanceFrequency(&freq); ready = 1; }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (unsigned long long)((c.QuadPart * 1000ULL) / freq.QuadPart);
}

#endif 
