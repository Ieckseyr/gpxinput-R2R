// engine
#ifndef GP_ENGINE_H
#define GP_ENGINE_H

#include <windows.h>
#include "gp_ipc.h"
#include "gp_real.h"

namespace gp_engine {



void Start(void);
bool SecondaryInstance(void);


void Stop(void);






DWORD OnSetState(DWORD controller, WORD left, WORD right, GpFnSetState downstream);



bool OnDeviceIoControl(DWORD ioctlCode, LPVOID inBuffer, DWORD inSize);
bool IsSelfWrite(HANDLE hDevice);


bool IsVibrationIoctl(DWORD code);


int PulseMode(void);

}  

#endif 
