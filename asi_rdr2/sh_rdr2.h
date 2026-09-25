

#ifndef SH_RDR2_H
#define SH_RDR2_H

#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace sh {

typedef void      (*FnScriptRegister)(HMODULE module, void (*fn)(void));
typedef void      (*FnScriptUnregister)(HMODULE module);
typedef void      (*FnScriptWait)(unsigned long waitTime);
typedef void      (*FnNativeInit)(uint64_t hash);
typedef void      (*FnNativePush64)(uint64_t value);
typedef uint64_t* (*FnNativeCall)(void);
typedef uint64_t* (*FnGetGlobalPtr)(int globalId);

inline FnScriptRegister  scriptRegister  = nullptr;
inline FnScriptUnregister scriptUnregister = nullptr;
inline FnScriptWait      scriptWait      = nullptr;
inline FnNativeInit      nativeInit      = nullptr;
inline FnNativePush64    nativePush64    = nullptr;
inline FnNativeCall      nativeCall      = nullptr;
inline FnGetGlobalPtr    getGlobalPtr    = nullptr;









inline const char* FindExportByPrefix(HMODULE h, const char* prefix) {
    const BYTE* base = (const BYTE*)h;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) return nullptr;

    const IMAGE_EXPORT_DIRECTORY* exp =
        (const IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    const DWORD* names = (const DWORD*)(base + exp->AddressOfNames);
    size_t len = 0;
    while (prefix[len]) ++len;

    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* n = (const char*)(base + names[i]);
        if (strncmp(n, prefix, len) == 0) return n;
    }
    return nullptr;
}


inline void* Take(HMODULE h, const char* decoratedPrefix, const char* plain) {
    const char* real = FindExportByPrefix(h, decoratedPrefix);
    void* p = real ? GetProcAddress(h, real) : nullptr;
    if (!p) p = GetProcAddress(h, plain);
    return p;
}



inline const char* Report(void) {
    static char buf[512];
    HMODULE h = GetModuleHandleW(L"ScriptHookRDR2.dll");
    if (!h) {
        lstrcpynA(buf, "ScriptHookRDR2.dll 未加载", sizeof(buf));
        return buf;
    }
    char sample[200] = {0};
    int written = 0;
    const BYTE* base = (const BYTE*)h;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        const IMAGE_EXPORT_DIRECTORY* exp =
            (const IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
        const DWORD* names = (const DWORD*)(base + exp->AddressOfNames);
        DWORD n = exp->NumberOfNames < 3 ? exp->NumberOfNames : 3;
        for (DWORD i = 0; i < n; ++i) {
            written += sprintf(sample + written, "%s%s",
                               i ? " " : "", (const char*)(base + names[i]));
        }
    }
    sprintf(buf, "scriptRegister=%s nativeInit=%s nativeCall=%s 导出示例: %s",
              scriptRegister ? "OK" : "无", nativeInit ? "OK" : "无",
              nativeCall ? "OK" : "无", sample);
    return buf;
}


inline bool Resolve(void) {
    HMODULE h = GetModuleHandleW(L"ScriptHookRDR2.dll");
    if (!h) return false;

    scriptRegister   = (FnScriptRegister)  Take(h, "?scriptRegister@@",   "scriptRegister");
    scriptUnregister = (FnScriptUnregister)Take(h, "?scriptUnregister@@YAXPEAUHINSTANCE__@@",
                                                   "scriptUnregister");
    scriptWait       = (FnScriptWait)      Take(h, "?scriptWait@@",       "scriptWait");
    nativeInit       = (FnNativeInit)      Take(h, "?nativeInit@@",       "nativeInit");
    nativePush64     = (FnNativePush64)    Take(h, "?nativePush64@@",     "nativePush64");
    nativeCall       = (FnNativeCall)      Take(h, "?nativeCall@@",       "nativeCall");
    getGlobalPtr     = (FnGetGlobalPtr)    Take(h, "?getGlobalPtr@@",     "getGlobalPtr");

    

    return scriptRegister && nativeInit && nativePush64 && nativeCall;
}

}  



inline uint64_t g_resultMask = ~0ull;


inline uint64_t rdr2_call0(uint64_t hash) {
    sh::nativeInit(hash);
    uint64_t* r = sh::nativeCall();
    return r ? (*r & g_resultMask) : 0;
}

inline uint64_t rdr2_call1(uint64_t hash, uint64_t a1) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    uint64_t* r = sh::nativeCall();
    return r ? (*r & g_resultMask) : 0;
}

inline uint64_t rdr2_call2(uint64_t hash, uint64_t a1, uint64_t a2) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    sh::nativePush64(a2);
    uint64_t* r = sh::nativeCall();
    return r ? (*r & g_resultMask) : 0;
}

inline uint64_t rdr2_call3(uint64_t hash, uint64_t a1, uint64_t a2, uint64_t a3) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    sh::nativePush64(a2);
    sh::nativePush64(a3);
    uint64_t* r = sh::nativeCall();
    return r ? (*r & g_resultMask) : 0;
}

inline uint64_t rdr2_callArgs(uint64_t hash, const uint64_t* a, int n) {
    sh::nativeInit(hash);
    for (int i = 0; i < n; ++i) sh::nativePush64(a[i]);
    uint64_t* r = sh::nativeCall();
    return r ? (*r & g_resultMask) : 0;
}



inline uint64_t* rdr2_callArgsPtr(uint64_t hash, const uint64_t* a, int n) {
    sh::nativeInit(hash);
    for (int i = 0; i < n; ++i) sh::nativePush64(a[i]);
    return sh::nativeCall();
}

#endif 
