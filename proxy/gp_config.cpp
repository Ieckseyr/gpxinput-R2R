// config
#include "gp_config.h"
#include "gp_log.h"

#include <string.h>
#include <wchar.h>

namespace {








int ReadInt(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def) {
    if (!ini || !ini[0]) return def;

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (!buf[0]) return def;

    wchar_t* end = nullptr;
    long v = wcstol(buf, &end, 0);   
    if (end == buf) return def;      
    return (int)v;
}







uint32_t ReadU32(const wchar_t* ini, const wchar_t* section, const wchar_t* key, uint32_t def) {
    if (!ini || !ini[0]) return def;

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (!buf[0]) return def;

    wchar_t* end = nullptr;
    unsigned long v = wcstoul(buf, &end, 0);   
    if (end == buf) return def;
    return (uint32_t)v;                        
}



int ReadLogLevel(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def) {
    if (!ini || !ini[0]) return def;

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (!buf[0]) return def;

    if (!_wcsicmp(buf, L"off"))   return 0;
    if (!_wcsicmp(buf, L"error")) return 1;
    if (!_wcsicmp(buf, L"info"))  return 2;
    if (!_wcsicmp(buf, L"debug")) return 3;
    if (!_wcsicmp(buf, L"trace")) return 4;

    wchar_t* end = nullptr;
    long v = wcstol(buf, &end, 0);
    if (end == buf) return def;
    return (int)v;
}

BOOL ReadBool(const wchar_t* ini, const wchar_t* section, const wchar_t* key, BOOL def) {
    if (!ini || !ini[0]) return def;
    wchar_t buf[32] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 32, ini);
    if (!buf[0]) return def;
    if (!_wcsicmp(buf, L"true") || !_wcsicmp(buf, L"yes") || !_wcsicmp(buf, L"1")) return TRUE;
    if (!_wcsicmp(buf, L"false") || !_wcsicmp(buf, L"no") || !_wcsicmp(buf, L"0")) return FALSE;
    return def;
}

void ReadString(const wchar_t* ini, const wchar_t* section, const wchar_t* key,
                const wchar_t* def, wchar_t* out, DWORD outChars) {
    if (!ini || !ini[0]) {
        wcsncpy_s(out, outChars, def, _TRUNCATE);
        return;
    }
    GetPrivateProfileStringW(section, key, def, out, outChars, ini);
}



float ReadFloat(const wchar_t* ini, const wchar_t* section, const wchar_t* key, float def) {
    if (!ini || !ini[0]) return def;

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (!buf[0]) return def;

    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf) return def;      
    return (float)v;
}



int ParseIoctlList(const wchar_t* text, DWORD* out, int maxOut) {
    int n = 0;
    const wchar_t* p = text;
    while (*p && n < maxOut) {
        while (*p && (*p == L' ' || *p == L',' || *p == L';' || *p == L'\t')) ++p;
        if (!*p) break;

        wchar_t* end = nullptr;
        unsigned long v = wcstoul(p, &end, 0);   
        if (end == p) break;                     

        out[n++] = (DWORD)v;
        p = end;
    }
    return n;
}


void BuildIniPath(const wchar_t* selfPath, wchar_t* out, DWORD outChars) {
    wcsncpy_s(out, outChars, selfPath, _TRUNCATE);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) slash = wcsrchr(out, L'/');
    if (slash) *(slash + 1) = 0;
    wcsncat_s(out, outChars, L"gpxinput.ini", _TRUNCATE);
}

}  

void GpConfigDefaults(GpProxyConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    


    cfg->logLevel             = 2;   
    cfg->mode                 = 0;   
    cfg->output               = 0;   
    cfg->triggers             = FALSE;
    cfg->timeoutMs            = 2;
    cfg->outputRateHz         = 125;
    cfg->hookDeviceIoControl  = TRUE;
    cfg->ioctlModify          = FALSE;
    cfg->ioctlDumpPath[0]     = 0;
    cfg->capturePath[0]       = 0;
    cfg->forceFfbCapability   = FALSE;
    cfg->hookRealXInput       = TRUE;
    cfg->extraIoctlCount      = 0;
    cfg->pulse                = 0;

    

    cfg->hidVendorIds[0]      = 0x045E;
    

    cfg->hidAllowBluetooth    = FALSE;
    cfg->hidVendorIdCount     = 1;
    cfg->hidAnyGamepad        = FALSE;
    cfg->iniFound             = FALSE;

    

    GpDefaultHapticsSettings(&cfg->haptics);

    cfg->monitorEnable    = TRUE;    
    cfg->monitorAutoStart = FALSE;   
    cfg->debugMode            = FALSE;
    cfg->debugMonitor         = TRUE;
    cfg->debugStateView       = TRUE;
    cfg->debugVerboseLog      = TRUE;
    cfg->debugToolDir[0]      = 0;
    cfg->monitorExe[0]    = 0;
}

void GpConfigLoad(GpProxyConfig* cfg) {
    if (!cfg) return;

    





    GpConfigDefaults(cfg);

    GP_LOG_TRACE("config: 取自身模块路径");
    GetModuleFileNameW(nullptr, cfg->selfPath, MAX_PATH);
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&GpConfigLoad, &self);
        GP_LOG_TRACE("config: 模块句柄 = %p", self);
        GetModuleFileNameW(self, cfg->selfPath, MAX_PATH);
    }

    BuildIniPath(cfg->selfPath, cfg->iniPath, MAX_PATH);
    GP_LOG_TRACE("config: ini 路径 = %s", gplog::W(cfg->iniPath));

    cfg->iniFound = GetFileAttributesW(cfg->iniPath) != INVALID_FILE_ATTRIBUTES;
    GP_LOG_TRACE("config: ini 存在 = %d", cfg->iniFound);

    

    const wchar_t* ini = cfg->iniPath;

    GP_LOG_TRACE("config: 开始读键值");
    cfg->logLevel            = ReadLogLevel(ini, L"Proxy", L"LogLevel", cfg->logLevel);
    cfg->mode                = ReadInt(ini, L"Proxy", L"Mode", cfg->mode);
    cfg->output              = ReadInt(ini, L"Proxy", L"Output", cfg->output);
    cfg->triggers            = ReadBool(ini, L"Proxy", L"Triggers", cfg->triggers);
    cfg->timeoutMs           = ReadInt(ini, L"Proxy", L"TimeoutMs", cfg->timeoutMs);
    cfg->outputRateHz        = ReadInt(ini, L"Proxy", L"OutputRateHz", cfg->outputRateHz);
    cfg->hookDeviceIoControl = ReadBool(ini, L"Proxy", L"HookDeviceIoControl",
                                        cfg->hookDeviceIoControl);
    cfg->hookRealXInput      = ReadBool(ini, L"Proxy", L"HookRealXInput",
                                        cfg->hookRealXInput);
    cfg->pulse               = ReadBool(ini, L"Proxy", L"Pulse", cfg->pulse != 0) ? 1 : 0;
    cfg->hidAnyGamepad       = ReadBool(ini, L"Proxy", L"HidAnyGamepad", cfg->hidAnyGamepad);
    cfg->ioctlModify         = ReadBool(ini, L"Proxy", L"IoctlModify", cfg->ioctlModify);
    cfg->forceFfbCapability  = ReadBool(ini, L"Proxy", L"ForceFFBCapability",
                                        cfg->forceFfbCapability);
    ReadString(ini, L"Proxy", L"IoctlDumpFile", L"", cfg->ioctlDumpPath, MAX_PATH);
    ReadString(ini, L"Proxy", L"CaptureFile", L"", cfg->capturePath, MAX_PATH);

    
    cfg->monitorEnable    = ReadBool(ini, L"Proxy", L"MonitorEnable", cfg->monitorEnable);
    cfg->monitorAutoStart = ReadBool(ini, L"Proxy", L"MonitorAutoStart", cfg->monitorAutoStart);

    

    {
        cfg->debugMode       = ReadBool(ini, L"Debug", L"Mode", cfg->debugMode);
        cfg->debugMonitor    = ReadBool(ini, L"Debug", L"Monitor", cfg->debugMonitor);
        cfg->debugStateView  = ReadBool(ini, L"Debug", L"StateView", cfg->debugStateView);
        cfg->debugVerboseLog = ReadBool(ini, L"Debug", L"VerboseLog", cfg->debugVerboseLog);
        ReadString(ini, L"Debug", L"ToolDir", L"", cfg->debugToolDir, MAX_PATH);

        wchar_t flag[MAX_PATH] = {0};
        wcsncpy_s(flag, MAX_PATH, ini, _TRUNCATE);
        wchar_t* slash = wcsrchr(flag, L'\\');
        if (slash) {
            slash[1] = 0;
            wcsncat_s(flag, MAX_PATH, L"gpxinput_debug.on", _TRUNCATE);
            if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) {
                cfg->debugMode = TRUE;
                cfg->logLevel  = 4;      
                GP_LOG_INFO("config: 调试模式来自 gpxinput_debug.on（与 ini 同目录）");
            } else if (cfg->debugMode) {
                GP_LOG_INFO("config: 调试模式来自 [Debug] Mode=true");
            }
        }

        
        if (cfg->debugMode && cfg->debugVerboseLog && cfg->logLevel < 3)
            cfg->logLevel = 3;
    }
    ReadString(ini, L"Proxy", L"MonitorExe", L"", cfg->monitorExe, MAX_PATH);

    

    {
        GpHapticsSettings& h = cfg->haptics;
        h.enable           = ReadBool(ini, L"Haptics", L"SelfHaptics", h.enable);
        h.driveLeftMotor   = ReadBool(ini, L"Haptics", L"DriveLeftMotor", h.driveLeftMotor);
        h.driveRightMotor  = ReadBool(ini, L"Haptics", L"DriveRightMotor", h.driveRightMotor);
        h.driveLeftTrigger = ReadBool(ini, L"Haptics", L"DriveLeftTrigger", h.driveLeftTrigger);
        h.driveRightTrigger= ReadBool(ini, L"Haptics", L"DriveRightTrigger", h.driveRightTrigger);
        h.shotReplaceGame = ReadBool(ini, L"Haptics", L"ShotReplaceGame", h.shotReplaceGame);
        h.shotFromTrigger  = ReadBool(ini, L"Haptics", L"ShotFromTrigger", h.shotFromTrigger);
        h.triggerPressThresh = ReadFloat(ini, L"Haptics", L"TriggerPressThresh", h.triggerPressThresh);
        h.triggerReleaseHyst = ReadFloat(ini, L"Haptics", L"TriggerReleaseHyst", h.triggerReleaseHyst);
        h.triggerRefractoryMs = ReadInt(ini, L"Haptics", L"TriggerRefractoryMs", h.triggerRefractoryMs);
        h.shotFromRumble   = ReadBool(ini, L"Haptics", L"ShotFromRumble", h.shotFromRumble);
        h.shotRiseThresh   = ReadFloat(ini, L"Haptics", L"ShotRiseThresh", h.shotRiseThresh);
        h.shotGain         = ReadFloat(ini, L"Haptics", L"ShotGain", h.shotGain);
        h.shotEnvMs        = ReadInt(ini, L"Haptics", L"ShotEnvMs", h.shotEnvMs);
        h.shotRefractoryMs = ReadInt(ini, L"Haptics", L"ShotRefractoryMs", h.shotRefractoryMs);
        h.shotSide         = ReadInt(ini, L"Haptics", L"ShotSide", h.shotSide);
        h.shotBodyKick     = ReadFloat(ini, L"Haptics", L"ShotBodyKick", h.shotBodyKick);
        h.shotTrigL        = ReadFloat(ini, L"Haptics", L"ShotTrigL", h.shotTrigL);
        h.shotTrigR        = ReadFloat(ini, L"Haptics", L"ShotTrigR", h.shotTrigR);
        h.shotBodyL        = ReadFloat(ini, L"Haptics", L"ShotBodyL", h.shotBodyL);
        h.shotBodyR        = ReadFloat(ini, L"Haptics", L"ShotBodyR", h.shotBodyR);
        h.shotAttackMs     = ReadInt(ini, L"Haptics", L"ShotAttackMs", h.shotAttackMs);
        h.shotBodyScale    = ReadFloat(ini, L"Haptics", L"ShotBodyScale", h.shotBodyScale);
        h.shotTrigGain     = ReadFloat(ini, L"Haptics", L"ShotTrigGain", h.shotTrigGain);
        h.shotTrigFloor    = ReadFloat(ini, L"Haptics", L"ShotTrigFloor", h.shotTrigFloor);
        h.shotTrigCeil     = ReadFloat(ini, L"Haptics", L"ShotTrigCeil", h.shotTrigCeil);
        h.shotDecayExp     = ReadFloat(ini, L"Haptics", L"ShotDecayExp", h.shotDecayExp);
        h.shotEnvScale     = ReadFloat(ini, L"Haptics", L"ShotEnvScale", h.shotEnvScale);
        h.rideShotBoost    = ReadFloat(ini, L"Haptics", L"RideShotBoost", h.rideShotBoost);
        h.rideSpeedLow     = ReadFloat(ini, L"Haptics", L"RideSpeedLow", h.rideSpeedLow);
        h.rideSpeedHigh    = ReadFloat(ini, L"Haptics", L"RideSpeedHigh", h.rideSpeedHigh);
        h.rideCurve        = ReadFloat(ini, L"Haptics", L"RideCurve", h.rideCurve);
        h.rideAmpMax       = ReadFloat(ini, L"Haptics", L"RideAmpMax", h.rideAmpMax);
        h.rideAmpMin       = ReadFloat(ini, L"Haptics", L"RideAmpMin", h.rideAmpMin);
        h.rideReplaceGame  = ReadBool(ini, L"Haptics", L"RideReplaceGame", h.rideReplaceGame);
        h.slowMoStretch    = ReadFloat(ini, L"Haptics", L"SlowMoStretch", h.slowMoStretch);
        h.slowMoWheel      = ReadFloat(ini, L"Haptics", L"SlowMoWheel", h.slowMoWheel);
        h.slowMoDeadEye    = ReadFloat(ini, L"Haptics", L"SlowMoDeadEye", h.slowMoDeadEye);
        h.slowMoEagle      = ReadFloat(ini, L"Haptics", L"SlowMoEagle", h.slowMoEagle);
        h.mountJumpGain    = ReadFloat(ini, L"Haptics", L"MountJumpGain", h.mountJumpGain);
        h.mountJumpEnvMs   = ReadInt(ini, L"Haptics", L"MountJumpEnvMs", h.mountJumpEnvMs);
        h.mountLandGain    = ReadFloat(ini, L"Haptics", L"MountLandGain", h.mountLandGain);
        h.mountLandEnvMs   = ReadInt(ini, L"Haptics", L"MountLandEnvMs", h.mountLandEnvMs);
        h.mountLandHeight  = ReadFloat(ini, L"Haptics", L"MountLandHeight", h.mountLandHeight);
        h.spookGain        = ReadFloat(ini, L"Haptics", L"SpookGain", h.spookGain);
        h.mountLandRise    = ReadFloat(ini, L"Haptics", L"MountLandRise", h.mountLandRise);
        h.spookEnvMs       = ReadInt(ini, L"Haptics", L"SpookEnvMs", h.spookEnvMs);
        h.spookAccel       = ReadFloat(ini, L"Haptics", L"SpookAccel", h.spookAccel);
        h.rideBodyBase     = ReadFloat(ini, L"Haptics", L"RideBodyBase", h.rideBodyBase);
        h.rideFadeMs       = ReadFloat(ini, L"Haptics", L"RideFadeMs", h.rideFadeMs);
        h.rideBeats        = ReadInt(ini, L"Haptics", L"RideBeats", h.rideBeats);
        h.rideBeatsWalk    = ReadInt(ini, L"Haptics", L"RideBeatsWalk", h.rideBeatsWalk);
        h.rideBeatsTrot    = ReadInt(ini, L"Haptics", L"RideBeatsTrot", h.rideBeatsTrot);
        h.rideBeatsCanter  = ReadInt(ini, L"Haptics", L"RideBeatsCanter", h.rideBeatsCanter);
        h.rideBeatsGallop  = ReadInt(ini, L"Haptics", L"RideBeatsGallop", h.rideBeatsGallop);
        h.rideTrotMaxSpeed = ReadFloat(ini, L"Haptics", L"RideTrotMaxSpeed", h.rideTrotMaxSpeed);
        h.ridePeriodCurve  = ReadFloat(ini, L"Haptics", L"RidePeriodCurve", h.ridePeriodCurve);
        h.rideBeatAccent   = ReadFloat(ini, L"Haptics", L"RideBeatAccent", h.rideBeatAccent);

        h.rideEnable       = ReadBool(ini, L"Haptics", L"RideEnable", h.rideEnable);
        h.ridePeakThresh   = ReadFloat(ini, L"Haptics", L"RidePeakThresh", h.ridePeakThresh);
        h.rideGain         = ReadFloat(ini, L"Haptics", L"RideGain", h.rideGain);
        h.rideTrigGain     = ReadFloat(ini, L"Haptics", L"RideTrigGain", h.rideTrigGain);
        h.rideMinPeriodMs  = ReadInt(ini, L"Haptics", L"RideMinPeriodMs", h.rideMinPeriodMs);
        h.rideMaxPeriodMs  = ReadInt(ini, L"Haptics", L"RideMaxPeriodMs", h.rideMaxPeriodMs);
        h.ridePeriodTol    = ReadFloat(ini, L"Haptics", L"RidePeriodTol", h.ridePeriodTol);
        h.rideHoldMs       = ReadInt(ini, L"Haptics", L"RideHoldMs", h.rideHoldMs);

        h.trigToBody       = ReadFloat(ini, L"Haptics", L"TrigToBody", h.trigToBody);

        
        h.useGameState = ReadBool(ini, L"Haptics", L"UseGameState", h.useGameState);
        h.aimBreathHz  = ReadFloat(ini, L"Haptics", L"AimBreathHz", h.aimBreathHz);
        h.aimBothTriggers  = ReadBool(ini, L"Haptics", L"AimBothTriggers", h.aimBothTriggers);
        h.aimEnable        = ReadBool(ini, L"Haptics", L"AimEnable", h.aimEnable);
        h.aimTrigScale     = ReadFloat(ini, L"Haptics", L"AimTrigScale", h.aimTrigScale);
        h.aimBodyScale     = ReadFloat(ini, L"Haptics", L"AimBodyScale", h.aimBodyScale);
        h.aimTriggerLevel = ReadFloat(ini, L"Haptics", L"AimTriggerLevel", h.aimTriggerLevel);
        h.aimHoldMs       = ReadInt(ini, L"Haptics", L"AimHoldMs", h.aimHoldMs);
        h.aimRampMs       = ReadFloat(ini, L"Haptics", L"AimRampMs", h.aimRampMs);
        h.aimRampGain     = ReadFloat(ini, L"Haptics", L"AimRampGain", h.aimRampGain);
        h.aimRampCurve     = ReadFloat(ini, L"Haptics", L"AimRampCurve", h.aimRampCurve);
        h.tickGain        = ReadFloat(ini, L"Haptics", L"TickGain", h.tickGain);
        h.tickEnvMs       = ReadInt(ini, L"Haptics", L"TickEnvMs", h.tickEnvMs);
        h.drawGain        = ReadFloat(ini, L"Haptics", L"DrawGain", h.drawGain);
        h.bowDrawGain      = ReadFloat(ini, L"Haptics", L"BowDrawGain", h.bowDrawGain);
        h.landGain         = ReadFloat(ini, L"Haptics", L"LandGain", h.landGain);
        h.landEnvMs        = ReadInt(ini, L"Haptics", L"LandEnvMs", h.landEnvMs);
        h.injuryGain       = ReadFloat(ini, L"Haptics", L"InjuryGain", h.injuryGain);
        h.injuryEnvMs      = ReadInt(ini, L"Haptics", L"InjuryEnvMs", h.injuryEnvMs);
        h.lowHealthHeartbeat = ReadFloat(ini, L"Haptics", L"LowHealthHeartbeat", h.lowHealthHeartbeat);
        h.deadeyeGain      = ReadFloat(ini, L"Haptics", L"DeadeyeGain", h.deadeyeGain);
        h.deadeyeEnvMs     = ReadInt(ini, L"Haptics", L"DeadeyeEnvMs", h.deadeyeEnvMs);
        h.bowDrawRampMs    = ReadInt(ini, L"Haptics", L"BowDrawRampMs", h.bowDrawRampMs);
        h.reloadGain       = ReadFloat(ini, L"Haptics", L"ReloadGain", h.reloadGain);
        h.reloadEnvMs      = ReadInt(ini, L"Haptics", L"ReloadEnvMs", h.reloadEnvMs);
        h.reloadSide       = ReadInt(ini, L"Haptics", L"ReloadSide", h.reloadSide);
        h.drawEnvMs       = ReadInt(ini, L"Haptics", L"DrawEnvMs", h.drawEnvMs);
        h.ltPressEnable   = ReadBool(ini, L"Haptics", L"LtPressEnable", h.ltPressEnable);
        h.ltPressGain     = ReadFloat(ini, L"Haptics", L"LtPressGain", h.ltPressGain);
        h.ltPressEnvMs    = ReadInt(ini, L"Haptics", L"LtPressEnvMs", h.ltPressEnvMs);

        


        for (int i = 0; i < GP_WEAPON_SLOTS; ++i) {
            wchar_t sec[24];
            swprintf_s(sec, L"Weapon%d", i);
            GpWeaponProfile& p = h.weapon[i];

            

            p.hash         = ReadU32(ini, sec, L"Hash", p.hash);
            p.shotGain     = ReadFloat(ini, sec, L"ShotGain", p.shotGain);
            p.shotEnvMs    = ReadInt(ini, sec, L"ShotEnvMs", p.shotEnvMs);
            p.shotBodyKick = ReadFloat(ini, sec, L"ShotBodyKick", p.shotBodyKick);
            p.trigL        = ReadFloat(ini, sec, L"ShotTrigL", p.trigL);
            p.trigR        = ReadFloat(ini, sec, L"ShotTrigR", p.trigR);
            p.bodyL        = ReadFloat(ini, sec, L"ShotBodyL", p.bodyL);
            p.bodyR        = ReadFloat(ini, sec, L"ShotBodyR", p.bodyR);
            p.attackMs     = ReadInt(ini, sec, L"ShotAttackMs", p.attackMs);
            p.aimTrig      = ReadFloat(ini, sec, L"AimTrig", p.aimTrig);
            p.aimBody      = ReadFloat(ini, sec, L"AimBody", p.aimBody);
            p.aimWobble    = ReadFloat(ini, sec, L"AimWobble", p.aimWobble);

            wchar_t nm[32] = {0};
            ReadString(ini, sec, L"Name", L"", nm, 32);
            if (nm[0]) {
                WideCharToMultiByte(CP_UTF8, 0, nm, -1, p.name, (int)sizeof(p.name),
                                    nullptr, nullptr);
            }
            if (p.hash != 0 || p.name[0]) h.weaponCount = i + 1;
        }

        



        for (int i = 0; i < GP_GUN_SLOTS; ++i) {
            wchar_t sec[24];
            swprintf_s(sec, L"Gun%d", i);
            GpWeaponProfile& p = h.gun[i];

            p.hash         = ReadU32(ini, sec, L"Hash", p.hash);
            if (p.hash == 0) continue;          
            p.shotGain     = ReadFloat(ini, sec, L"ShotGain", p.shotGain);
            p.shotEnvMs    = ReadInt(ini, sec, L"ShotEnvMs", p.shotEnvMs);
            p.shotBodyKick = ReadFloat(ini, sec, L"ShotBodyKick", p.shotBodyKick);
            p.trigL        = ReadFloat(ini, sec, L"ShotTrigL", p.trigL);
            p.trigR        = ReadFloat(ini, sec, L"ShotTrigR", p.trigR);
            p.bodyL        = ReadFloat(ini, sec, L"ShotBodyL", p.bodyL);
            p.bodyR        = ReadFloat(ini, sec, L"ShotBodyR", p.bodyR);
            p.attackMs     = ReadInt(ini, sec, L"ShotAttackMs", p.attackMs);
            p.aimTrig      = ReadFloat(ini, sec, L"AimTrig", p.aimTrig);
            p.aimBody      = ReadFloat(ini, sec, L"AimBody", p.aimBody);
            p.aimWobble    = ReadFloat(ini, sec, L"AimWobble", p.aimWobble);

            wchar_t nm[32] = {0};
            ReadString(ini, sec, L"Name", L"", nm, 32);
            if (nm[0]) {
                WideCharToMultiByte(CP_UTF8, 0, nm, -1, p.name, (int)sizeof(p.name),
                                    nullptr, nullptr);
            }
            h.gunCount = i + 1;
        }
    }

    


    {
        wchar_t vids[256] = {0};
        ReadString(ini, L"Proxy", L"HidVendorIds", L"", vids, 256);
        if (vids[0]) {
            DWORD parsed[8];
            int n = ParseIoctlList(vids, parsed, 8);
            if (n > 0) {
                cfg->hidVendorIdCount = n;
                for (int i = 0; i < n; ++i) cfg->hidVendorIds[i] = parsed[i];
            }
        }
    }

    GP_LOG_TRACE("config: 读 ExtraVibrationIoctls");
    wchar_t extra[512] = {0};
    ReadString(ini, L"Proxy", L"ExtraVibrationIoctls", L"", extra, 512);
    cfg->extraIoctlCount = ParseIoctlList(extra, cfg->extraIoctls, 8);

    
    if (cfg->logLevel < 0) cfg->logLevel = 0;
    if (cfg->logLevel > 4) cfg->logLevel = 4;

    if (cfg->mode < 0) cfg->mode = 0;
    if (cfg->mode > 2) cfg->mode = 2;

    if (cfg->output < 0) cfg->output = 0;
    if (cfg->output > 2) cfg->output = 2;

    if (cfg->timeoutMs < 0)    cfg->timeoutMs = 0;
    if (cfg->timeoutMs > 50)   cfg->timeoutMs = 50;

    GP_LOG_TRACE("config: 全部键读完，夹紧区间");
    if (cfg->outputRateHz < 30)   cfg->outputRateHz = 30;
    if (cfg->outputRateHz > 1000) cfg->outputRateHz = 1000;
}
