// hid
#ifndef GP_HID_H
#define GP_HID_H

#include <windows.h>

namespace gphid {


struct Device {
    HANDLE  hDevice;              
    wchar_t path[512];            
    wchar_t instanceId[512];      
    USHORT  vid;
    USHORT  pid;
    BYTE    reportId;             
    BOOL    bluetooth;
    int     outputReportBytes;
    BOOL    usable;               
    BOOL    gamepadUsage;         
    BOOL    trustedFormat;        

    



    int     ioctlKind;
};









void SetVendorFilter(const DWORD* vendorIds, int count, BOOL anyGamepad);
void SetBluetoothAllowed(BOOL on);
BOOL IsOurHandle(HANDLE h);



int Init(void);

void Shutdown(void);


int Rescan(void);

int Count(void);


const wchar_t* Describe(int index);


bool IsBluetooth(int index);




bool Send(int index, BYTE leftMotor, BYTE rightMotor,
          BYTE leftTrigger, BYTE rightTrigger, int pulseMode);



bool SendMapped(int controllerIndex, BYTE leftMotor, BYTE rightMotor,
                BYTE leftTrigger, BYTE rightTrigger, int pulseMode);
















int  IoctlCount(void);
bool SendIoctlAll(BYTE leftMotor, BYTE rightMotor,
                  BYTE leftTrigger, BYTE rightTrigger, int pulseMode);

}  

#endif 
