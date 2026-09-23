// real
#ifndef GP_REAL_H
#define GP_REAL_H

#include <windows.h>





typedef struct GpXInputGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} GpXInputGamepad;

typedef struct GpXInputState {
    DWORD            dwPacketNumber;
    GpXInputGamepad  Gamepad;
} GpXInputState;

typedef struct GpXInputVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} GpXInputVibration;

typedef struct GpXInputCapabilities {
    BYTE              Type;
    BYTE              SubType;
    WORD              Flags;
    GpXInputGamepad   Gamepad;
    GpXInputVibration Vibration;
} GpXInputCapabilities;

typedef struct GpXInputBatteryInformation {
    BYTE BatteryType;
    BYTE BatteryLevel;
} GpXInputBatteryInformation;

typedef struct GpXInputKeystroke {
    WORD  VirtualKey;
    WCHAR Unicode;
    WORD  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
} GpXInputKeystroke;





typedef DWORD (WINAPI *GpFnGetState)(DWORD, GpXInputState*);
typedef DWORD (WINAPI *GpFnSetState)(DWORD, GpXInputVibration*);
typedef DWORD (WINAPI *GpFnGetCapabilities)(DWORD, DWORD, GpXInputCapabilities*);
typedef void  (WINAPI *GpFnEnable)(BOOL);
typedef DWORD (WINAPI *GpFnGetBatteryInformation)(DWORD, BYTE, GpXInputBatteryInformation*);
typedef DWORD (WINAPI *GpFnGetKeystroke)(DWORD, DWORD, GpXInputKeystroke*);
typedef DWORD (WINAPI *GpFnGetAudioDeviceIds)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD (WINAPI *GpFnGetDSoundAudioDeviceGuids)(DWORD, GUID*, GUID*);




typedef DWORD (WINAPI *GpFnGetStateEx)(DWORD, GpXInputState*);
typedef DWORD (WINAPI *GpFnOrdinal4)(DWORD, void*, void*, void*);

namespace gpreal {




bool Load(void);



bool SelfLoadDetected(void);


void SetForceFfb(bool on);
bool ForceFfb(void);



bool Injected(void);


const wchar_t* RealPath(void);


const wchar_t* SelfPath(void);


const wchar_t* SelfName(void);


bool Ready(void);



GpFnSetState SetStateDirect(void);


GpFnSetState SetStateCall(void);


void SetSetStateTrampoline(GpFnSetState trampoline);


GpFnGetState               GetState(void);
GpFnGetCapabilities        GetCapabilities(void);
GpFnEnable                 EnableFn(void);
GpFnGetBatteryInformation  GetBatteryInformation(void);
GpFnGetKeystroke           GetKeystroke(void);
GpFnGetAudioDeviceIds      GetAudioDeviceIds(void);
GpFnGetDSoundAudioDeviceGuids GetDSoundAudioDeviceGuids(void);


void* ResolveOrdinal(WORD ordinal);

}  









extern "C" {



BOOL WINAPI gp_exp_DllMain(HINSTANCE, DWORD, LPVOID);

DWORD WINAPI gp_exp_XInputGetState(DWORD dwUserIndex, GpXInputState* pState);
DWORD WINAPI gp_exp_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags,
                                          GpXInputCapabilities* pCapabilities);
void  WINAPI gp_exp_XInputEnable(BOOL enable);
DWORD WINAPI gp_exp_XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType,
                                                GpXInputBatteryInformation* pBatteryInformation);
DWORD WINAPI gp_exp_XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved,
                                       GpXInputKeystroke* pKeystroke);
DWORD WINAPI gp_exp_XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId,
                                            UINT* pRenderCount, LPWSTR pCaptureDeviceId,
                                            UINT* pCaptureCount);
DWORD WINAPI gp_exp_XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex,
                                                    GUID* pDSoundRenderGuid,
                                                    GUID* pDSoundCaptureGuid);

DWORD WINAPI gp_exp_ordinal100(DWORD dwUserIndex, GpXInputState* pState);


DWORD WINAPI gp_exp_ordinal101(DWORD a, DWORD b, void* c);
DWORD WINAPI gp_exp_ordinal102(DWORD a);
DWORD WINAPI gp_exp_ordinal103(DWORD a);

DWORD WINAPI gp_exp_ordinal104(DWORD a, void* b);

DWORD WINAPI gp_exp_ordinal108(DWORD a, DWORD b, DWORD c, void* d);

DWORD WINAPI gp_exp_ordinal109(void* a, void* b, void* c, void* d);

}  

#endif 
