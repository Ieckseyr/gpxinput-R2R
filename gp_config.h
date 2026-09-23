// config
#ifndef GP_CONFIG_H
#define GP_CONFIG_H

#include <windows.h>

#include "gp_haptics.h"

struct GpProxyConfig {
    
    int         logLevel;

    
    int         mode;

    
    int         output;

    
    BOOL        triggers;

    
    int         timeoutMs;

    
    int         outputRateHz;

    

    BOOL        hookDeviceIoControl;

    








    BOOL        ioctlModify;

    











    BOOL        forceFfbCapability;

    







    wchar_t     ioctlDumpPath[MAX_PATH];

    







    wchar_t     capturePath[MAX_PATH];

    

    BOOL        hookRealXInput;

    
    DWORD       extraIoctls[8];
    int         extraIoctlCount;

    

    int         pulse;

    








    DWORD       hidVendorIds[8];
    

    BOOL        hidAllowBluetooth;
    int         hidVendorIdCount;

    

    BOOL        hidAnyGamepad;

    
    wchar_t     iniPath[MAX_PATH];
    BOOL        iniFound;

    
    wchar_t     selfPath[MAX_PATH];

    




    GpHapticsSettings haptics;

    



    BOOL        monitorEnable;

    

    BOOL        monitorAutoStart;
    wchar_t     monitorExe[MAX_PATH];   

    









    BOOL        debugMode;
    BOOL        debugMonitor;      
    BOOL        debugStateView;    
    BOOL        debugVerboseLog;   
    wchar_t     debugToolDir[MAX_PATH];  
};


void GpConfigDefaults(GpProxyConfig* cfg);


void GpConfigLoad(GpProxyConfig* cfg);

#endif 
