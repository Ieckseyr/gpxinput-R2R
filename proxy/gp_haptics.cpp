// haptics
#include "gp_haptics.h"
#include "gp_log.h"

#include <math.h>
#include <string.h>

namespace {

const uint32_t kMaxControllers = 4;





const float kBaselineTauMs = 300.0f;


const float kShotAttackMs = 6.0f;


const DWORD kPeakMinGapMs = 140;


const int kPeakHistory = 6;

struct CtrlState {
    
    DWORD lastTick;
    BYTE  prevL, prevR;

    
    float baseL, baseR;
    DWORD baseTick;

    
    DWORD shotStart;
    float shotAmp;
    float shotTrigL, shotTrigR, shotBodyL, shotBodyR;
    int   shotAttackMs;
    BOOL  shotActive;
    DWORD lastShotTick;
    

    int   shotEnvMs;
    float shotBodyKick;

    
    BOOL     stateValid;
    BOOL     aiming;
    uint32_t stateGroup;    
    uint32_t lastWeapon;    
    int32_t  lastAmmo;
    BOOL     wasShooting;
    uint8_t  menuActive;    
    uint8_t  armed;         

    
    BOOL     ltDown;
    DWORD    ltDownTick;
    DWORD    aimStart;

    
    BOOL     auxActive;
    DWORD    auxStart;
    float    auxAmp;
    int      auxEnvMs;
    int      auxSide;

    
    DWORD peakTick[kPeakHistory];
    float peakAmp[kPeakHistory];
    int   peakCount;
    DWORD lastPeakTick;
    float rideAmp;
    DWORD rideUntil;
    
    BOOL  wasReloading;
    BOOL  wasAirborne;    
    BOOL  wasSlowMo;      
    int   lastHealth;     
    DWORD bowDrawStart;
    BOOL  aimActiveNow, bowActiveNow;       
    BOOL  stateSeenOnce;   
    BOOL  onMount;
    uint8_t horseGait;   
    BOOL    wasMountJump;
    BOOL    wasMountHurt;
    DWORD   lastSpookTick;   
    DWORD   decelSince;      
    BOOL    driftFired, rearFired;
    DWORD   lastDriftTick, lastRearTick;
    DWORD   lastLandTick;    

    
    DWORD   vehNextTick;     
    int     vehBeat;         
    float   vehLevel;        
    float   vehBody;         
    DWORD   trainPassUntil;  
    float   trainPassLevel;
    BOOL    wasTrainNear;
    float   prevMountHeight;   
    DWORD   airborneSince;     
    float   peakAirHeight;     
    BOOL    uiOverlay;     
    BOOL    slowMotion;    
    float   slowStretch;   
    float horseSpeed;
    DWORD rideNextTick;
    float rideLevel;      
    DWORD rideLastTick;
    int   rideBeat;
    int   rideBeats;
    BOOL  rideLogged;     

    
    BYTE  prevLT, prevRT;
    BYTE  trigPeak;       
    BOOL  padValid;
    BOOL  trigArmed;      
    BYTE  padLT, padRT;   
};

CtrlState  g_c[kMaxControllers];
GpHapticsSettings g_s;
BOOL g_applied = FALSE;

inline BYTE ClampByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (BYTE)(v + 0.5f);
}

inline float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}



inline void UpdateBaseline(float* base, float value, float dtMs) {
    if (dtMs <= 0.0f) { *base = value; return; }
    float a = dtMs / (kBaselineTauMs + dtMs);
    *base += (value - *base) * a;
}








const GpWeaponProfile* FindProfile(uint32_t group, uint32_t weapon) {
    for (int i = 0; i < g_s.gunCount && i < GP_GUN_SLOTS; ++i) {
        if (weapon != 0 && g_s.gun[i].hash != 0 && g_s.gun[i].hash == weapon)
            return &g_s.gun[i];
    }
    if (group == 0) return nullptr;
    for (int i = 0; i < g_s.weaponCount && i < GP_WEAPON_SLOTS; ++i) {
        if (g_s.weapon[i].hash != 0 && g_s.weapon[i].hash == group) return &g_s.weapon[i];
    }
    return nullptr;
}



void FireAux(CtrlState* cs, DWORD now, float amp, int envMs, int side) {
    cs->auxActive = TRUE;
    cs->auxStart  = now;
    cs->auxAmp    = Clamp01(amp);
    cs->auxEnvMs  = envMs < 10 ? 10 : envMs;
    cs->auxSide   = side;
}



void FireShot(CtrlState* cs, DWORD now, float amp, const GpWeaponProfile* prof) {
    cs->shotActive = TRUE;
    cs->shotStart  = now;
    cs->shotAmp     = Clamp01(amp);
    cs->shotTrigL   = prof ? prof->trigL   : g_s.shotTrigL;
    cs->shotTrigR   = prof ? prof->trigR   : g_s.shotTrigR;
    cs->shotBodyL   = prof ? prof->bodyL   : g_s.shotBodyL;
    cs->shotBodyR   = prof ? prof->bodyR   : g_s.shotBodyR;
    cs->shotAttackMs = prof ? prof->attackMs : g_s.shotAttackMs;
    cs->lastShotTick = now;

    if (prof) {
        cs->shotEnvMs    = prof->shotEnvMs > 0 ? prof->shotEnvMs : g_s.shotEnvMs;
        cs->shotBodyKick = prof->shotBodyKick;
    } else {
        cs->shotEnvMs    = g_s.shotEnvMs;
        cs->shotBodyKick = g_s.shotBodyKick;
    }
    if (cs->shotEnvMs < 10) cs->shotEnvMs = 10;

    

    {
        float sc = g_s.shotEnvScale > 0.0f ? g_s.shotEnvScale : 1.0f;
        cs->shotEnvMs = (int)((float)cs->shotEnvMs * sc);
        if (cs->shotEnvMs < 20) cs->shotEnvMs = 20;
        if (cs->shotEnvMs > 900) cs->shotEnvMs = 900;
    }
}

CtrlState* State(uint32_t controller) {
    if (controller >= kMaxControllers) return nullptr;
    return &g_c[controller];
}

void PushPeak(CtrlState* cs, DWORD tick, float amp) {
    if (cs->peakCount < kPeakHistory) {
        cs->peakTick[cs->peakCount] = tick;
        cs->peakAmp[cs->peakCount] = amp;
        cs->peakCount++;
        return;
    }
    
    for (int i = 1; i < kPeakHistory; ++i) {
        cs->peakTick[i - 1] = cs->peakTick[i];
        cs->peakAmp[i - 1] = cs->peakAmp[i];
    }
    cs->peakTick[kPeakHistory - 1] = tick;
    cs->peakAmp[kPeakHistory - 1] = amp;
}



DWORD DetectGait(CtrlState* cs, DWORD now) {
    

    if (cs->peakCount < 5) return 0;

    DWORD iv[kPeakHistory];
    int n = 0;
    for (int i = 1; i < cs->peakCount; ++i) {
        DWORD d = cs->peakTick[i] - cs->peakTick[i - 1];
        if (d < (DWORD)g_s.rideMinPeriodMs || d > (DWORD)g_s.rideMaxPeriodMs) return 0;
        iv[n++] = d;
    }
    if (n < 3) return 0;

    
    DWORD sorted[kPeakHistory];
    memcpy(sorted, iv, sizeof(DWORD) * (size_t)n);
    for (int i = 1; i < n; ++i) {
        DWORD v = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; --j; }
        sorted[j + 1] = v;
    }
    DWORD median = sorted[n / 2];

    

    for (int i = 0; i < n; ++i) {
        float dev = (float)((long)iv[i] - (long)median);
        if (dev < 0) dev = -dev;
        if (dev > (float)median * g_s.ridePeriodTol) return 0;
    }

    
    float sum = 0.0f;
    int cnt = 0;
    for (int i = 0; i < cs->peakCount; ++i) { sum += cs->peakAmp[i]; ++cnt; }
    float amp = cnt ? sum / (float)cnt : 0.0f;

    cs->rideAmp = amp;
    cs->rideUntil = now + (DWORD)g_s.rideHoldMs;
    cs->lastPeakTick = cs->peakTick[cs->peakCount - 1];

    if (!cs->rideLogged) {
        cs->rideLogged = TRUE;
        GP_LOG_INFO("haptics: 检出步态 周期=%ums 幅度=%.2f —— 开始输出骑乘辅助",
                    (unsigned)median, amp);
    }
    return median;
}


DWORD g_ridePeriod[kMaxControllers];

}  

void GpDefaultHapticsSettings(GpHapticsSettings* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->enable           = FALSE;   

    s->shotFromTrigger  = TRUE;
    s->triggerPressThresh = 0.55f;
    s->triggerReleaseHyst = 0.20f;
    s->triggerRefractoryMs = 150;
    s->shotFromRumble   = FALSE;   

    s->shotRiseThresh   = 0.20f;

    

    s->gunCount = 0;
    for (int k = 0; k < GP_GUN_SLOTS; ++k) {
        s->gun[k].shotGain  = 1.0f;
        s->gun[k].shotEnvMs = 100;
        s->gun[k].attackMs  = 8;
    }

    s->driveLeftMotor   = TRUE;
    s->driveRightMotor  = TRUE;
    s->driveLeftTrigger = TRUE;
    s->driveRightTrigger= TRUE;
    s->shotReplaceGame  = TRUE;
    s->shotBodyScale    = 0.60f;
    s->shotTrigGain     = 1.10f;
    s->shotTrigFloor    = 90.0f;
    s->shotTrigCeil     = 130.0f;   
    s->shotDecayExp     = 1.20f;
    s->shotEnvScale     = 1.80f;   
    s->shotGain         = 1.0f;
    s->shotEnvMs        = 90;
    s->shotRefractoryMs = 55;
    s->shotSide         = 0;
    s->shotBodyKick     = 0.15f;
    s->shotTrigL        = 0.0f;
    s->shotTrigR        = 160.0f;
    s->shotBodyL        = 60.0f;
    s->shotBodyR        = 80.0f;
    s->shotAttackMs     = 8;
    


    s->rideShotBoost    = 2.5f;

    s->rideEnable       = TRUE;
    s->ridePeakThresh   = 0.10f;
    s->rideSpeedLow     = 1.0f;
    s->rideCurve        = 1.6f;
    s->rideAmpMax       = 0.35f;
    s->rideAmpMin       = 0.20f;   
    s->rideFadeMs       = 700.0f;
    s->rideBeats        = 4;
    s->rideBeatsWalk    = 4;
    s->rideBeatsTrot    = 2;
    s->rideBeatsCanter  = 3;
    s->rideBeatsGallop  = 4;
    s->rideTrotMaxSpeed = 6.0f;
    s->ridePeriodCurve  = 0.60f;
    s->rideBeatAccent   = 0.6f;
    s->rideSpeedHigh    = 9.0f;
    s->rideGain         = 0.9f;
    s->rideTrigGain     = 0.35f;
    s->rideMinPeriodMs  = 200;
    s->rideMaxPeriodMs  = 900;
    s->ridePeriodTol    = 0.35f;
    s->rideHoldMs       = 1200;

    s->trigToBody       = 0.45f;

    
    s->useGameState = TRUE;
    s->aimEnable        = FALSE;
    s->aimTrigScale     = 1.0f;
    s->aimBodyScale     = 1.0f;
    s->aimBothTriggers  = FALSE;
    s->aimBreathHz  = 0.4f;   
    s->aimTriggerLevel = 0.24f;  
    s->aimHoldMs       = 300;    
    

    s->aimRampMs       = 60000.0f;
    s->aimRampGain     = 5.00f;
    s->aimRampCurve    = 3.00f;
    s->tickGain        = 0.35f;  
    s->tickEnvMs       = 45;
    s->ltPressEnable   = FALSE;
    s->ltPressGain     = 0.55f;  
    s->ltPressEnvMs    = 90;
    s->bowDrawGain     = 0.28f;
    s->bowDrawRampMs   = 1100;
    s->reloadGain      = 0.0f;   
    s->reloadEnvMs     = 70;
    s->reloadSide      = 2;
    s->drawGain        = 0.55f;  
    s->drawEnvMs       = 80;

    


    struct Def { uint32_t hash; const char* name; float sg; int env; float kick;
                 float at; float ab; float wob; };
    static const Def kDef[GP_WEAPON_SLOTS] = {
        { GPRDR2_GRP_PISTOL,   "Pistol",   0.80f,  70, 0.12f, 0.06f, 0.04f, 0.50f },
        { GPRDR2_GRP_REVOLVER, "Revolver", 0.95f,  80, 0.15f, 0.07f, 0.05f, 0.60f },
        { GPRDR2_GRP_REPEATER, "Repeater", 1.00f,  85, 0.15f, 0.07f, 0.05f, 0.50f },
        { GPRDR2_GRP_RIFLE,    "Rifle",    1.20f,  95, 0.18f, 0.09f, 0.06f, 0.45f },
        { GPRDR2_GRP_SHOTGUN,  "Shotgun",  1.60f, 120, 0.25f, 0.10f, 0.07f, 0.70f },
        { GPRDR2_GRP_SNIPER,   "Sniper",   1.35f, 110, 0.20f, 0.12f, 0.08f, 0.35f },
        { GPRDR2_GRP_BOW,      "Bow",      0.70f, 140, 0.10f, 0.05f, 0.03f, 0.80f },
        { GPRDR2_GRP_MELEE,    "Melee",    0.60f,  60, 0.10f, 0.00f, 0.00f, 0.00f },
    };
    for (int i = 0; i < GP_WEAPON_SLOTS; ++i) {
        s->weapon[i].hash         = kDef[i].hash;
        s->weapon[i].shotGain     = kDef[i].sg;
        s->weapon[i].shotEnvMs    = kDef[i].env;
        s->weapon[i].shotBodyKick = kDef[i].kick;
        s->weapon[i].aimTrig      = kDef[i].at;
        s->weapon[i].aimBody      = kDef[i].ab;
        s->weapon[i].aimWobble    = kDef[i].wob;
        strncpy_s(s->weapon[i].name, sizeof(s->weapon[i].name), kDef[i].name, _TRUNCATE);
    }
    s->weaponCount = GP_WEAPON_SLOTS;
}

void GpApplyHapticsSettings(const GpHapticsSettings& s) {
    g_s = s;
    g_applied = TRUE;

    
    if (g_s.shotRiseThresh < 0.01f) g_s.shotRiseThresh = 0.01f;
    if (g_s.shotRiseThresh > 1.0f)  g_s.shotRiseThresh = 1.0f;
    if (g_s.shotEnvMs < 10)  g_s.shotEnvMs = 10;
    if (g_s.shotEnvMs > 400) g_s.shotEnvMs = 400;
    if (g_s.shotRefractoryMs < 0)   g_s.shotRefractoryMs = 0;
    if (g_s.shotRefractoryMs > 500) g_s.shotRefractoryMs = 500;
    if (g_s.shotSide < 0 || g_s.shotSide > 2) g_s.shotSide = 0;
    if (g_s.triggerPressThresh < 0.1f) g_s.triggerPressThresh = 0.1f;
    if (g_s.triggerPressThresh > 0.95f) g_s.triggerPressThresh = 0.95f;
    if (g_s.triggerReleaseHyst < 0.0f) g_s.triggerReleaseHyst = 0.0f;
    if (g_s.triggerReleaseHyst > 0.5f) g_s.triggerReleaseHyst = 0.5f;
    if (g_s.triggerRefractoryMs < 0) g_s.triggerRefractoryMs = 0;
    if (g_s.triggerRefractoryMs > 2000) g_s.triggerRefractoryMs = 2000;
    if (g_s.rideShotBoost < 1.0f) g_s.rideShotBoost = 1.0f;
    if (g_s.rideShotBoost > 6.0f) g_s.rideShotBoost = 6.0f;
    if (g_s.rideMinPeriodMs < 60)  g_s.rideMinPeriodMs = 60;
    if (g_s.rideMaxPeriodMs > 3000) g_s.rideMaxPeriodMs = 3000;
    if (g_s.rideMaxPeriodMs <= g_s.rideMinPeriodMs) g_s.rideMaxPeriodMs = g_s.rideMinPeriodMs + 100;
    if (g_s.rideHoldMs < 0) g_s.rideHoldMs = 0;
    if (g_s.rideHoldMs > 10000) g_s.rideHoldMs = 10000;
    if (g_s.rideSpeedLow < 0.0f) g_s.rideSpeedLow = 0.0f;
    if (g_s.rideSpeedHigh <= g_s.rideSpeedLow + 0.5f)
        g_s.rideSpeedHigh = g_s.rideSpeedLow + 0.5f;
    if (g_s.rideCurve < 0.5f) g_s.rideCurve = 0.5f;
    if (g_s.rideCurve > 4.0f) g_s.rideCurve = 4.0f;
    if (g_s.rideAmpMax < 0.0f) g_s.rideAmpMax = 0.0f;
    if (g_s.rideAmpMax > 1.0f) g_s.rideAmpMax = 1.0f;
    if (g_s.rideAmpMin < 0.0f) g_s.rideAmpMin = 0.0f;
    if (g_s.rideAmpMin > g_s.rideAmpMax) g_s.rideAmpMin = g_s.rideAmpMax;
    if (g_s.rideBodyBase < 0.0f) g_s.rideBodyBase = 0.0f;
    if (g_s.rideBodyBase > 0.5f) g_s.rideBodyBase = 0.5f;
    if (g_s.slowMoStretch < 1.0f) g_s.slowMoStretch = 1.0f;
    if (g_s.slowMoStretch > 20.0f) g_s.slowMoStretch = 20.0f;
    if (g_s.slowMoWheel < 1.0f) g_s.slowMoWheel = 1.0f;
    if (g_s.slowMoWheel > 20.0f) g_s.slowMoWheel = 20.0f;
    if (g_s.slowMoDeadEye < 1.0f) g_s.slowMoDeadEye = 1.0f;
    if (g_s.slowMoDeadEye > 20.0f) g_s.slowMoDeadEye = 20.0f;
    if (g_s.slowMoEagle < 1.0f) g_s.slowMoEagle = 1.0f;
    if (g_s.slowMoEagle > 20.0f) g_s.slowMoEagle = 20.0f;
    if (g_s.mountJumpGain < 0.0f) g_s.mountJumpGain = 0.0f;
    if (g_s.mountJumpEnvMs < 10) g_s.mountJumpEnvMs = 10;
    if (g_s.mountLandGain < 0.0f) g_s.mountLandGain = 0.0f;
    if (g_s.mountLandEnvMs < 10) g_s.mountLandEnvMs = 10;
    if (g_s.mountLandHeight < 0.3f) g_s.mountLandHeight = 0.3f;
    if (g_s.spookGain < 0.0f) g_s.spookGain = 0.0f;
    if (g_s.spookEnvMs < 10) g_s.spookEnvMs = 10;
    if (g_s.spookAccel > -1.0f) g_s.spookAccel = -1.0f;
    if (g_s.mountLandRise < 0.1f) g_s.mountLandRise = 0.1f;
    if (g_s.mountLandRise > 1.0f) g_s.mountLandRise = 1.0f;
    if (g_s.wagonGain < 0.0f) g_s.wagonGain = 0.0f;
    if (g_s.wagonMinPeriodMs < 60) g_s.wagonMinPeriodMs = 60;
    if (g_s.wagonMaxPeriodMs <= g_s.wagonMinPeriodMs) g_s.wagonMaxPeriodMs = g_s.wagonMinPeriodMs + 60;
    if (g_s.wagonSpeedHigh <= g_s.wagonSpeedLow + 0.5f) g_s.wagonSpeedHigh = g_s.wagonSpeedLow + 0.5f;
    if (g_s.wagonBodyBase < 0.0f) g_s.wagonBodyBase = 0.0f;
    if (g_s.wagonBodyBase > 0.5f) g_s.wagonBodyBase = 0.5f;
    if (g_s.trainRideGain < 0.0f) g_s.trainRideGain = 0.0f;
    if (g_s.trainRideMinPeriodMs < 60) g_s.trainRideMinPeriodMs = 60;
    if (g_s.trainRideMaxPeriodMs <= g_s.trainRideMinPeriodMs)
        g_s.trainRideMaxPeriodMs = g_s.trainRideMinPeriodMs + 60;
    if (g_s.trainRideBodyBase < 0.0f) g_s.trainRideBodyBase = 0.0f;
    if (g_s.trainRideBodyBase > 0.5f) g_s.trainRideBodyBase = 0.5f;
    if (g_s.trainPassGain < 0.0f) g_s.trainPassGain = 0.0f;
    if (g_s.trainPassRadius < 10.0f) g_s.trainPassRadius = 10.0f;
    if (g_s.trainPassApproachRef < 1.0f) g_s.trainPassApproachRef = 1.0f;
    if (g_s.trainPassEnvMs < 100) g_s.trainPassEnvMs = 100;
    if (g_s.rideFadeMs < 0.0f) g_s.rideFadeMs = 0.0f;
    if (g_s.rideFadeMs > 5000.0f) g_s.rideFadeMs = 5000.0f;
    if (g_s.rideBeats < 1) g_s.rideBeats = 1;
    if (g_s.rideBeats > 8) g_s.rideBeats = 8;
    if (g_s.rideBeatsWalk < 1) g_s.rideBeatsWalk = 1;
    if (g_s.rideBeatsTrot < 1) g_s.rideBeatsTrot = 1;
    if (g_s.rideBeatsCanter < 1) g_s.rideBeatsCanter = 1;
    if (g_s.rideBeatsGallop < 1) g_s.rideBeatsGallop = 1;
    if (g_s.rideBeatsWalk > 8) g_s.rideBeatsWalk = 8;
    if (g_s.rideBeatsTrot > 8) g_s.rideBeatsTrot = 8;
    if (g_s.rideBeatsCanter > 8) g_s.rideBeatsCanter = 8;
    if (g_s.rideBeatsGallop > 8) g_s.rideBeatsGallop = 8;
    if (g_s.rideTrotMaxSpeed < 1.0f) g_s.rideTrotMaxSpeed = 1.0f;
    if (g_s.ridePeriodCurve < 0.2f) g_s.ridePeriodCurve = 0.2f;
    if (g_s.ridePeriodCurve > 3.0f) g_s.ridePeriodCurve = 3.0f;
    if (g_s.rideBeatAccent < 0.0f) g_s.rideBeatAccent = 0.0f;
    if (g_s.rideBeatAccent > 1.0f) g_s.rideBeatAccent = 1.0f;

    if (g_s.aimTriggerLevel < 0.05f) g_s.aimTriggerLevel = 0.05f;
    if (g_s.aimTriggerLevel > 0.9f) g_s.aimTriggerLevel = 0.9f;
    if (g_s.aimHoldMs < 0) g_s.aimHoldMs = 0;
    if (g_s.aimHoldMs > 2000) g_s.aimHoldMs = 2000;
    if (g_s.aimTrigScale < 0.0f) g_s.aimTrigScale = 0.0f;
    if (g_s.aimTrigScale > 10.0f) g_s.aimTrigScale = 10.0f;
    if (g_s.aimBodyScale < 0.0f) g_s.aimBodyScale = 0.0f;
    if (g_s.aimBodyScale > 10.0f) g_s.aimBodyScale = 10.0f;
    if (g_s.aimRampMs < 100.0f) g_s.aimRampMs = 100.0f;
    if (g_s.aimRampGain < 0.0f) g_s.aimRampGain = 0.0f;
    if (g_s.aimRampGain > 20.0f) g_s.aimRampGain = 20.0f;
    if (g_s.aimRampCurve < 0.5f) g_s.aimRampCurve = 0.5f;
    if (g_s.aimRampCurve > 8.0f) g_s.aimRampCurve = 8.0f;
    if (g_s.tickEnvMs < 10) g_s.tickEnvMs = 10;
    if (g_s.drawEnvMs < 10) g_s.drawEnvMs = 10;
    if (g_s.landGain < 0.0f) g_s.landGain = 0.0f;
    if (g_s.landEnvMs < 10) g_s.landEnvMs = 10;
    if (g_s.injuryGain < 0.0f) g_s.injuryGain = 0.0f;
    if (g_s.injuryEnvMs < 10) g_s.injuryEnvMs = 10;
    if (g_s.lowHealthHeartbeat < 0.0f) g_s.lowHealthHeartbeat = 0.0f;
    if (g_s.deadeyeGain < 0.0f) g_s.deadeyeGain = 0.0f;
    if (g_s.deadeyeEnvMs < 10) g_s.deadeyeEnvMs = 10;
    if (g_s.bowDrawGain < 0.0f) g_s.bowDrawGain = 0.0f;
    if (g_s.bowDrawGain > 1.0f) g_s.bowDrawGain = 1.0f;
    if (g_s.bowDrawRampMs < 100) g_s.bowDrawRampMs = 100;
    if (g_s.bowDrawRampMs > 5000) g_s.bowDrawRampMs = 5000;
    if (g_s.shotTrigGain < 0.0f) g_s.shotTrigGain = 0.0f;
    if (g_s.shotTrigGain > 2.0f) g_s.shotTrigGain = 2.0f;
    if (g_s.shotTrigFloor < 0.0f) g_s.shotTrigFloor = 0.0f;
    if (g_s.shotTrigCeil > 255.0f) g_s.shotTrigCeil = 255.0f;
    if (g_s.shotTrigCeil < g_s.shotTrigFloor) g_s.shotTrigCeil = g_s.shotTrigFloor;
    if (g_s.shotBodyScale < 0.0f) g_s.shotBodyScale = 0.0f;
    if (g_s.shotBodyScale > 2.0f) g_s.shotBodyScale = 2.0f;
    if (g_s.shotEnvScale < 0.2f) g_s.shotEnvScale = 0.2f;
    if (g_s.shotEnvScale > 5.0f) g_s.shotEnvScale = 5.0f;
    if (g_s.shotDecayExp < 0.3f) g_s.shotDecayExp = 0.3f;
    if (g_s.shotDecayExp > 4.0f) g_s.shotDecayExp = 4.0f;
    if (g_s.reloadGain < 0.0f) g_s.reloadGain = 0.0f;
    if (g_s.reloadEnvMs < 10) g_s.reloadEnvMs = 10;
    if (g_s.reloadEnvMs > 500) g_s.reloadEnvMs = 500;
    if (g_s.aimBreathHz < 0.0f) g_s.aimBreathHz = 0.0f;
    if (g_s.aimBreathHz > 5.0f) g_s.aimBreathHz = 5.0f;
    if (g_s.weaponCount < 0) g_s.weaponCount = 0;
    if (g_s.weaponCount > GP_WEAPON_SLOTS) g_s.weaponCount = GP_WEAPON_SLOTS;
    if (g_s.gunCount < 0) g_s.gunCount = 0;
    if (g_s.gunCount > GP_GUN_SLOTS) g_s.gunCount = GP_GUN_SLOTS;
    for (int i = 0; i < g_s.gunCount; ++i) {
        if (g_s.gun[i].shotEnvMs < 10) g_s.gun[i].shotEnvMs = 10;
        if (g_s.gun[i].shotEnvMs > 400) g_s.gun[i].shotEnvMs = 400;
        if (g_s.gun[i].shotGain < 0.0f) g_s.gun[i].shotGain = 0.0f;
        if (g_s.gun[i].shotGain > 4.0f) g_s.gun[i].shotGain = 4.0f;
        if (g_s.gun[i].trigL < 0.0f) g_s.gun[i].trigL = 0.0f;
        if (g_s.gun[i].trigR < 0.0f) g_s.gun[i].trigR = 0.0f;
        if (g_s.gun[i].bodyL < 0.0f) g_s.gun[i].bodyL = 0.0f;
        if (g_s.gun[i].bodyR < 0.0f) g_s.gun[i].bodyR = 0.0f;
        if (g_s.gun[i].bodyR > 255.0f) g_s.gun[i].bodyR = 255.0f;
        if (g_s.gun[i].trigR > 255.0f) g_s.gun[i].trigR = 255.0f;
        if (g_s.gun[i].bodyL > 255.0f) g_s.gun[i].bodyL = 255.0f;
        if (g_s.gun[i].trigL > 255.0f) g_s.gun[i].trigL = 255.0f;
        if (g_s.gun[i].attackMs < 1) g_s.gun[i].attackMs = 1;
        if (g_s.gun[i].attackMs > 100) g_s.gun[i].attackMs = 100;
        g_s.gun[i].name[sizeof(g_s.gun[i].name) - 1] = 0;
    }
    for (int i = 0; i < g_s.weaponCount; ++i) {
        if (g_s.weapon[i].shotEnvMs < 10) g_s.weapon[i].shotEnvMs = 10;
        if (g_s.weapon[i].shotEnvMs > 400) g_s.weapon[i].shotEnvMs = 400;
        if (g_s.weapon[i].shotGain < 0.0f) g_s.weapon[i].shotGain = 0.0f;
        if (g_s.weapon[i].shotGain > 4.0f) g_s.weapon[i].shotGain = 4.0f;
        if (g_s.weapon[i].trigL < 0.0f) g_s.weapon[i].trigL = 0.0f;
        if (g_s.weapon[i].trigR < 0.0f) g_s.weapon[i].trigR = 0.0f;
        if (g_s.weapon[i].bodyL < 0.0f) g_s.weapon[i].bodyL = 0.0f;
        if (g_s.weapon[i].bodyR < 0.0f) g_s.weapon[i].bodyR = 0.0f;
        if (g_s.weapon[i].trigR > 255.0f) g_s.weapon[i].trigR = 255.0f;
        if (g_s.weapon[i].bodyL > 255.0f) g_s.weapon[i].bodyL = 255.0f;
        if (g_s.weapon[i].bodyR > 255.0f) g_s.weapon[i].bodyR = 255.0f;
        if (g_s.weapon[i].trigL > 255.0f) g_s.weapon[i].trigL = 255.0f;
        if (g_s.weapon[i].attackMs < 1) g_s.weapon[i].attackMs = 1;
        g_s.weapon[i].name[sizeof(g_s.weapon[i].name) - 1] = 0;
    }

    GP_LOG_INFO("haptics: 自合成%s 开枪(接管=%s 扳机判据=%s 扣下阈值=%.2f / 波形判据=%s 阈值=%.2f 增益=%.2f 时长=%dms 侧=%d 体感=%.2f) "
                "骑乘(%s 增益=%.2f 扳机=%.2f 周期=%d~%dms 马速=%.1f~%.1f 曲线=%.1f) 无HID折算=%.2f",
                g_s.enable ? "开启" : "关闭", g_s.shotReplaceGame ? "是" : "否",
                g_s.shotFromTrigger ? "开" : "关", g_s.triggerPressThresh,
                g_s.shotFromRumble ? "开" : "关",
                g_s.shotRiseThresh, g_s.shotGain, g_s.shotEnvMs, g_s.shotSide, g_s.shotBodyKick,
                g_s.rideEnable ? "开" : "关", g_s.rideGain, g_s.rideTrigGain,
                g_s.rideMinPeriodMs, g_s.rideMaxPeriodMs,
                (double)g_s.rideSpeedLow, (double)g_s.rideSpeedHigh,
                (double)g_s.rideCurve, g_s.trigToBody);

    if (!g_s.driveLeftMotor || !g_s.driveRightMotor ||
        !g_s.driveLeftTrigger || !g_s.driveRightTrigger) {
        GP_LOG_INFO("haptics: 直通(我们不动) 左体感=%s 右体感=%s 左扳机=%s 右扳机=%s",
                    g_s.driveLeftMotor   ? "否" : "是", g_s.driveRightMotor  ? "否" : "是",
                    g_s.driveLeftTrigger ? "否" : "是", g_s.driveRightTrigger? "否" : "是");
    }

    GP_LOG_INFO("haptics: 具体枪械档=%d 套（优先于武器组）", g_s.gunCount);
    for (int i = 0; i < g_s.gunCount; ++i) {
        const GpWeaponProfile& g = g_s.gun[i];
        GP_LOG_INFO("haptics:   枪%d %-28s 0x%08X 开枪(强度=%.2f 时长=%dms 体感=%.2f)",
                    i, g.name, g.hash, g.shotGain, g.shotEnvMs, g.shotBodyKick);
    }
    GP_LOG_INFO("haptics: 游戏状态=%s 武器档=%d 套 瞄准起伏=%.2fHz",
                g_s.useGameState ? "采信" : "忽略", g_s.weaponCount, g_s.aimBreathHz);
    for (int i = 0; i < g_s.weaponCount; ++i) {
        const GpWeaponProfile& p = g_s.weapon[i];
        GP_LOG_INFO("haptics:   档%d %-9s 组=0x%08X 开枪(强度=%.2f 时长=%dms 体感=%.2f) "
                    "瞄准(扳机=%.2f 体感=%.2f 起伏=%.2f)",
                    i, p.name, p.hash, p.shotGain, p.shotEnvMs, p.shotBodyKick,
                    p.aimTrig, p.aimBody, p.aimWobble);
    }
}

void GpOnGameFrame(uint32_t controller, DWORD tick, BYTE bodyL, BYTE bodyR) {
    CtrlState* cs = State(controller);
    if (!cs) return;

    

    if (cs->lastTick == tick && cs->prevL == bodyL && cs->prevR == bodyR) return;

    DWORD now = tick ? tick : GetTickCount();
    if (cs->lastTick == 0) {
        cs->baseL = (float)bodyL;
        cs->baseR = (float)bodyR;
        cs->baseTick = now;
    }

    float dt = (float)(DWORD)(now - cs->baseTick);
    if (dt > 0.0f && dt < 1000.0f) {
        UpdateBaseline(&cs->baseL, (float)bodyL, dt);
        UpdateBaseline(&cs->baseR, (float)bodyR, dt);
        cs->baseTick = now;
    }

    
    float riseL = ((float)bodyL - cs->baseL) / 255.0f;
    float riseR = ((float)bodyR - cs->baseR) / 255.0f;

    




    BOOL risingL = bodyL > cs->prevL;
    BOOL risingR = bodyR > cs->prevR;

    float rise = 0.0f;
    int   side = 0;
    if (risingR && riseR >= riseL)      { rise = riseR; side = 0; }
    else if (risingL && riseL > riseR)  { rise = riseL; side = 1; }
    else if (risingR)                   { rise = riseR; side = 0; }
    else if (risingL)                   { rise = riseL; side = 1; }

    

    float prevRiseL = ((float)cs->prevL - cs->baseL) / 255.0f;
    float prevRiseR = ((float)cs->prevR - cs->baseR) / 255.0f;
    float prevRise  = prevRiseL > prevRiseR ? prevRiseL : prevRiseR;

    DWORD sinceShot = now - cs->lastShotTick;

    
    float thresh = g_s.shotRiseThresh;
    if (g_s.rideEnable && now < cs->rideUntil) thresh *= g_s.rideShotBoost;

    if (g_s.shotFromRumble &&
        rise >= thresh &&
        (prevRise < thresh * 0.6f || sinceShot > (DWORD)g_s.shotRefractoryMs) &&
        sinceShot >= (DWORD)g_s.shotRefractoryMs) {
        cs->shotActive = TRUE;
        cs->shotStart = now;
        cs->shotAmp = Clamp01(rise * g_s.shotGain);
        cs->lastShotTick = now;
        GP_LOG_DEBUG("haptics: 检测到冲击 幅度=%.2f 侧=%s (基线 L=%.0f R=%.0f 现值 L=%u R=%u)",
                     rise, side == 0 ? "右" : "左", cs->baseL, cs->baseR,
                     (unsigned)bodyL, (unsigned)bodyR);
    }

    




    if (g_s.rideEnable && cs->stateValid && cs->onMount &&
        now - cs->lastLandTick >= 600) {
        float riseMax = riseL > riseR ? riseL : riseR;
        if (riseMax >= g_s.mountLandRise) {
            float amp = Clamp01(riseMax) * g_s.mountLandGain;
            if (amp > 1.2f) amp = 1.2f;
            cs->lastLandTick = now;
            GpFireEffect(controller, GP_FX_DRAW, amp, g_s.mountLandEnvMs, 2);
            GP_LOG_INFO("haptics: 坐骑落地（声=%.2f）-> 力度 %.2f",
                        (double)riseMax, (double)amp);
        }
    }

    





    BOOL triggerBusy = (cs->padRT > 40) ||
                       (cs->lastShotTick != 0 && (now - cs->lastShotTick) < 300);

    

    BOOL stateRide = (g_s.useGameState && cs->stateValid && cs->onMount);

    if (g_s.rideEnable && !triggerBusy && !stateRide) {
        float peak = ((float)bodyL + (float)bodyR) / 2.0f / 255.0f;
        float prevPeak = ((float)cs->prevL + (float)cs->prevR) / 2.0f / 255.0f;
        

        if (peak >= g_s.ridePeakThresh && prevPeak < g_s.ridePeakThresh &&
            (cs->lastPeakTick == 0 || (now - cs->lastPeakTick) >= kPeakMinGapMs)) {
            PushPeak(cs, now, peak);
            cs->lastPeakTick = now;
            DWORD period = DetectGait(cs, now);
            if (period) g_ridePeriod[controller] = period;
        }
    }

    cs->prevL = bodyL;
    cs->prevR = bodyR;
    cs->lastTick = tick;
}

void GpOnPadInput(uint32_t controller, DWORD now, BYTE leftTrigger, BYTE rightTrigger) {
    CtrlState* cs = State(controller);
    if (!cs || !g_s.enable) return;

    cs->padLT = leftTrigger;
    cs->padRT = rightTrigger;

    if (!cs->padValid) {
        cs->prevLT = leftTrigger;
        cs->prevRT = rightTrigger;
        cs->trigPeak = rightTrigger;
        

        cs->trigArmed = (rightTrigger < 64);
        cs->padValid = TRUE;
        return;
    }

    







    
    float thresh = g_s.triggerPressThresh;
    if (g_s.rideEnable && now < cs->rideUntil) thresh = Clamp01(thresh + 0.25f);

    BYTE hi = (BYTE)(thresh * 255.0f + 0.5f);
    BYTE releasePx = (BYTE)(g_s.triggerReleaseHyst * 255.0f + 0.5f);
    if (releasePx < 8) releasePx = 8;

    if (rightTrigger > cs->trigPeak) cs->trigPeak = rightTrigger;
    if (cs->trigPeak > (BYTE)(releasePx + 16) &&
        rightTrigger <= (BYTE)(cs->trigPeak - releasePx)) {
        cs->trigArmed = TRUE;
        cs->trigPeak = rightTrigger;
    }

    DWORD sinceShot = now - cs->lastShotTick;

    

    BOOL stateShot = (g_s.useGameState && cs->stateValid);

    



    if (g_s.useGameState && !cs->stateSeenOnce) stateShot = TRUE;

    if (!stateShot && g_s.shotFromTrigger && cs->trigArmed && rightTrigger >= hi &&
        sinceShot >= (DWORD)g_s.triggerRefractoryMs) {
        const GpWeaponProfile* prof = FindProfile(cs->stateGroup, cs->lastWeapon);
        
        float amp = Clamp01((float)rightTrigger / 255.0f) * g_s.shotGain;
        FireShot(cs, now, amp, prof);
        cs->trigArmed = FALSE;
        cs->trigPeak = rightTrigger;
        GP_LOG_DEBUG("haptics: 扳机扣下 -> 开枪脉冲 (RT=%u 阈值=%u 强度=%.2f %s)",
                     (unsigned)rightTrigger, (unsigned)hi, amp,
                     prof ? prof->name : "通用");
    }

    





    BYTE ltTh = (BYTE)(g_s.aimTriggerLevel * 255.0f + 0.5f);
    BOOL ltNow = leftTrigger >= ltTh;
    if (ltNow && !cs->ltDown) {
        cs->ltDown = TRUE;
        cs->ltDownTick = now;
        if (g_s.ltPressEnable)
            GpFireEffect(controller, GP_FX_LT, g_s.ltPressGain, g_s.ltPressEnvMs, 1);
        GP_LOG_DEBUG("haptics: LT 按下 -> 反馈");
    } else if (!ltNow && cs->ltDown) {
        cs->ltDown = FALSE;
    }

    cs->prevLT = leftTrigger;
    cs->prevRT = rightTrigger;
}

void GpOnGameState(uint32_t controller, DWORD now, BOOL valid, const GpRdr2State* st) {
    CtrlState* cs = State(controller);
    if (!cs || !g_s.enable) return;

    if (!valid || !st) {
        

        cs->stateValid  = FALSE;
        cs->aiming      = FALSE;
        cs->onMount     = FALSE;
        cs->horseGait   = 255;
        cs->uiOverlay   = FALSE;
        cs->slowMotion  = FALSE;
        cs->prevMountHeight = 0.0f;
        cs->horseSpeed  = 0.0f;
        cs->rideNextTick = 0;
        cs->stateGroup  = 0;
        cs->lastWeapon  = 0;
        cs->lastAmmo    = 0;
        cs->wasShooting = FALSE;
        return;
    }

    cs->menuActive = st->menuActive != 0;
    cs->armed      = st->armed != 0;
    


    if (st->reloading && !cs->wasReloading) {
        GP_LOG_DEBUG("haptics: 开始装弹 -> 反馈（增益 %.2f）", (double)g_s.reloadGain);
        GpFireEffect(controller, GP_FX_RELOAD, g_s.reloadGain, g_s.reloadEnvMs, g_s.reloadSide);
    }
    cs->wasReloading = st->reloading != 0;

    cs->stateSeenOnce = TRUE;
    





    BOOL airborneNow = (st->jumping || st->falling || st->vaulting) != 0;
    if (cs->stateValid)
    {
        if (cs->wasAirborne && !airborneNow && st->grounded && g_s.landGain > 0.0f) {
            float amp = st->falling ? g_s.landGain : g_s.landGain * 0.6f;
            GpFireEffect(controller, GP_FX_DRAW, amp, g_s.landEnvMs, 0);
            GP_LOG_DEBUG("haptics: 落地 -> 反馈（坠落=%d）", (int)st->falling);
        }
        if (st->health > 0 && st->maxHealth > 0 &&
            st->health < cs->lastHealth && g_s.injuryGain > 0.0f) {
            GpFireEffect(controller, GP_FX_TICK, g_s.injuryGain, g_s.injuryEnvMs, 2);
            GP_LOG_DEBUG("haptics: 受伤（%d -> %d）-> 反馈", cs->lastHealth, (int)st->health);
        }
        BOOL slowNow = st->slowMotion != 0;
        if (slowNow && !cs->wasSlowMo && g_s.deadeyeGain > 0.0f) {
            GpFireEffect(controller, GP_FX_DRAW, g_s.deadeyeGain, g_s.deadeyeEnvMs, 2);
            GP_LOG_DEBUG("haptics: 进入慢动作（死眼/演出）-> 反馈");
        }
        cs->wasAirborne = airborneNow;
        cs->wasSlowMo   = slowNow;
        cs->lastHealth  = st->health;
    }
    else
    {
        cs->wasAirborne = airborneNow;
        cs->wasSlowMo   = st->slowMotion != 0;
        cs->lastHealth  = st->health;
    }

    cs->onMount    = st->onMount != 0;
    cs->horseGait  = st->horseGait;
    cs->uiOverlay  = st->uiOverlay != 0;
    cs->slowMotion = st->slowMotion != 0;

    

    if (cs->slowMotion) {
        float def = (g_s.slowMoStretch > 1.0f) ? g_s.slowMoStretch : 1.0f;
        if (cs->uiOverlay)          cs->slowStretch = g_s.slowMoWheel   > 1.0f ? g_s.slowMoWheel   : def;
        else if (cs->aiming)        cs->slowStretch = g_s.slowMoDeadEye > 1.0f ? g_s.slowMoDeadEye : def;
        else                        cs->slowStretch = g_s.slowMoEagle   > 1.0f ? g_s.slowMoEagle   : def;
    } else {
        cs->slowStretch = 1.0f;
    }

    




    if (cs->uiOverlay || st->menuActive) {
        cs->rideLevel    = 0.0f;
        cs->rideNextTick = 0;
        cs->rideAmp      = 0.0f;
    }

    
    BOOL mountJumpNow = st->mountJumping != 0;
    if (mountJumpNow && !cs->wasMountJump && g_s.mountJumpGain > 0.0f && !cs->uiOverlay) {
        GpFireEffect(controller, GP_FX_DRAW, g_s.mountJumpGain, g_s.mountJumpEnvMs, 2);
        GP_LOG_DEBUG("haptics: 坐骑起跳 -> 发力反馈");
    }
    cs->wasMountJump = mountJumpNow;

    







    float mh = st->mountHeight;
    BOOL mountAir = (st->mountJumping || st->mountFalling ||
                     mh >= g_s.mountLandHeight) != 0;

    if (mountAir) {
        if (cs->airborneSince == 0) cs->airborneSince = now;
    } else if (cs->airborneSince != 0) {
        DWORD airMs = now - cs->airborneSince;
        cs->airborneSince = 0;
        if (cs->onMount && airMs >= 150 && g_s.mountLandGain > 0.0f && !cs->uiOverlay &&
            now - cs->lastLandTick >= 600) {
            float peak = cs->peakAirHeight;
            float k = (peak - g_s.mountLandHeight) / 6.0f;   
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            float amp = g_s.mountLandGain * (0.65f + 0.6f * k);
            if (amp > 1.2f) amp = 1.2f;
            cs->lastLandTick = now;
            GpFireEffect(controller, GP_FX_DRAW, amp, g_s.mountLandEnvMs, 2);
            GP_LOG_INFO("haptics: 坐骑落地（腾空 %lums，最高 %.1fm）-> 力度 %.2f",
                        (unsigned long)airMs, (double)peak, (double)amp);
        }
    }
    if (mountAir && mh > cs->peakAirHeight) cs->peakAirHeight = mh;
    if (!mountAir) cs->peakAirHeight = 0.0f;
    cs->prevMountHeight = mh;

    





    {
        BOOL hardDecel = (st->horseAccel < -4.0f) && (st->horseSpeed > 3.0f);
        if (hardDecel) {
            if (cs->decelSince == 0) cs->decelSince = now;
            DWORD ms = now - cs->decelSince;
            if (ms > 350 && !cs->driftFired && now - cs->lastDriftTick >= 1200) {
                GpFireEffect(controller, GP_FX_TICK, 0.55f, 260, 2);
                cs->driftFired  = TRUE;
                cs->lastDriftTick = now;
                GP_LOG_INFO("haptics: 坐骑漂移/急刹 -> 滑动反馈（持续 %lums）", (unsigned long)ms);
            }
            if (st->horseSpeed < 2.5f && st->horseAccel < -9.0f &&
                !cs->rearFired && now - cs->lastRearTick >= 1500) {
                GpFireEffect(controller, GP_FX_DRAW, 0.90f, 150, 2);
                cs->rearFired = TRUE;
                cs->lastRearTick = now;
                GP_LOG_INFO("haptics: 坐骑人立 -> 反馈");
            }
        } else {
            cs->decelSince  = 0;
            cs->driftFired  = FALSE;
            cs->rearFired   = FALSE;
        }
    }

    


    if (cs->onMount && cs->stateValid && !cs->uiOverlay) {
        BOOL hurtNow = st->mountHurt != 0;
        if (hurtNow && !cs->wasMountHurt && g_s.spookGain > 0.0f) {
            GpFireEffect(controller, GP_FX_DRAW, g_s.spookGain, g_s.spookEnvMs, 2);
            GpFireEffect(controller, GP_FX_TICK, g_s.spookGain * 0.8f, g_s.spookEnvMs * 2, 2);
            GP_LOG_INFO("haptics: 坐骑受惊（受伤）-> 强反馈");
        }
        cs->wasMountHurt = hurtNow;

        if (st->horseAccel < g_s.spookAccel && st->horseSpeed > 2.0f &&
            now - cs->lastSpookTick >= 900) {
            GpFireEffect(controller, GP_FX_TICK, g_s.spookGain * 0.6f, g_s.spookEnvMs, 2);
            cs->lastSpookTick = now;
            GP_LOG_DEBUG("haptics: 坐骑急变（%.1f/s2）-> 弱反馈", (double)st->horseAccel);
        }
    }
    







    {
        BOOL stOk = cs->stateValid && !cs->uiOverlay;
        float vehBody = 0.0f;

        
        if (g_s.wagonEnable && stOk && st->inVehicle && !st->inTrain &&
            st->vehicleSpeed > g_s.wagonSpeedLow) {
            float k = (st->vehicleSpeed - g_s.wagonSpeedLow) /
                      (g_s.wagonSpeedHigh - g_s.wagonSpeedLow);
            k = Clamp01(k);

            
            DWORD period = (DWORD)(g_s.wagonMaxPeriodMs +
                                   (g_s.wagonMinPeriodMs - g_s.wagonMaxPeriodMs) * k);
            if (period < 60) period = 60;

            if (cs->vehNextTick == 0 || now >= cs->vehNextTick) {
                cs->vehNextTick = now + period;
                

                float amp = g_s.wagonGain * (0.35f + 0.65f * k);
                DWORD env = (DWORD)((float)period * 0.35f);
                if (env < 30) env = 30;
                GpFireEffect(controller, GP_FX_TICK, amp, (int)env, 2);
            }
            cs->vehBody = g_s.wagonBodyBase * k;      
            cs->vehLevel = k;
            cs->trainPassUntil = 0;                   
        }
        
        else if (g_s.trainRideEnable && stOk && st->inTrain) {
            float k = Clamp01(st->vehicleSpeed / 20.0f);
            DWORD period = (DWORD)(g_s.trainRideMaxPeriodMs +
                                   (g_s.trainRideMinPeriodMs - g_s.trainRideMaxPeriodMs) * k);
            if (period < 60) period = 60;
            

            DWORD second = (DWORD)((float)period * 0.4f);
            if (cs->vehNextTick == 0 || now >= cs->vehNextTick) {
                cs->vehNextTick = now + period;
                cs->vehBeat = 0;
                GpFireEffect(controller, GP_FX_TICK, g_s.trainRideGain * (0.5f + 0.5f * k), 60, 2);
            } else if (cs->vehBeat == 0 && now >= cs->vehNextTick - period + second) {
                cs->vehBeat = 1;
                GpFireEffect(controller, GP_FX_TICK, g_s.trainRideGain * 0.35f * (0.5f + 0.5f * k), 45, 2);
            }
            cs->vehBody = g_s.trainRideBodyBase * (0.4f + 0.6f * k);
            cs->vehLevel = k;
            cs->trainPassUntil = 0;
        }
        
        else {
            cs->vehNextTick = 0;
            cs->vehBody = 0.0f;
            cs->vehLevel = 0.0f;
        }

        


        BOOL passNow = FALSE;
        if (g_s.trainPassEnable && stOk && !st->inTrain && st->trainNearby &&
            st->trainApproach > 0.5f && st->trainDist < g_s.trainPassRadius) {
            float kApp = Clamp01(st->trainApproach / g_s.trainPassApproachRef);
            float kDist = Clamp01(1.0f - st->trainDist / g_s.trainPassRadius);
            float k = kApp * kDist;
            if (k > 0.05f) {
                passNow = TRUE;
                
                if (!cs->wasTrainNear) {
                    GpFireEffect(controller, GP_FX_DRAW, g_s.trainPassGain * 1.1f,
                                 g_s.trainPassEnvMs, 2);
                    GP_LOG_INFO("haptics: 火车呼啸而过（距离 %.1f 米，接近 %.1f 米/秒）",
                                (double)st->trainDist, (double)st->trainApproach);
                }
                cs->trainPassLevel = k;
                cs->trainPassUntil = now + g_s.trainPassEnvMs + 400;
            }
        }
        if (cs->trainPassUntil != 0 && now < cs->trainPassUntil && !passNow) {
            
            DWORD left = cs->trainPassUntil - now;
            cs->trainPassLevel = cs->trainPassLevel * 0.90f;
            if (left < 60) cs->trainPassLevel = 0.0f;
        } else if (!passNow) {
            cs->trainPassLevel = 0.0f;
            cs->trainPassUntil = 0;
        }
        if (cs->trainPassLevel > 0.0f) {
            vehBody += g_s.trainPassGain * cs->trainPassLevel;
        }
        cs->wasTrainNear = st->trainNearby ? TRUE : FALSE;

        if (vehBody > 1.0f) vehBody = 1.0f;
        cs->vehBody = Clamp01(cs->vehBody + vehBody);
    }

    cs->horseSpeed = st->horseSpeed;

    

    if (st->weaponHash != cs->lastWeapon) {
        

        {
            const GpWeaponProfile* gp = nullptr;
            for (int i = 0; i < g_s.gunCount && i < GP_GUN_SLOTS; ++i) {
                if (g_s.gun[i].hash == st->weaponHash && g_s.gun[i].hash != 0) {
                    gp = &g_s.gun[i];
                    break;
                }
            }
            if (gp) {
                GP_LOG_INFO("haptics: 换枪 0x%08X -> 「%s」（具体枪械档）",
                            st->weaponHash, gp->name);
            } else {
                GP_LOG_INFO("haptics: 换枪 0x%08X -> 未登记（组 0x%08X），"
                            "用武器组参数；想单独调就把它加进 [Gun*]",
                            st->weaponHash, st->weaponGroup);
            }
        }
        

        if (st->weaponHash != 0) {
            GpFireEffect(controller, GP_FX_DRAW, g_s.drawGain, g_s.drawEnvMs, 2);
            GP_LOG_DEBUG("haptics: 武器变为 0x%08X -> 掏枪反馈", st->weaponHash);
        }
        cs->lastWeapon  = st->weaponHash;
        cs->lastAmmo    = st->ammoInClip;
        cs->wasShooting = st->shooting != 0;
        cs->stateGroup  = st->weaponGroup;
        cs->stateValid  = TRUE;
        cs->aiming      = st->aiming != 0;
        return;
    }

    cs->stateValid = TRUE;
    cs->aiming     = st->aiming != 0;
    cs->stateGroup = st->weaponGroup;

    if (!g_s.useGameState) return;

    









    const GpWeaponProfile* prof = FindProfile(st->weaponGroup, st->weaponHash);

    





    BOOL noClip = (st->weaponGroup == GPRDR2_GRP_BOW ||
                   st->weaponGroup == GPRDR2_GRP_THROWN ||
                   st->weaponGroup == GPRDR2_GRP_MELEE);
    BOOL ammoDrop = noClip && cs->lastAmmo > 0 && st->ammoInClip < cs->lastAmmo;

    BOOL shootingNow = st->shooting != 0;
    if ((shootingNow && !cs->wasShooting) || ammoDrop ||
        (shootingNow && (now - cs->lastShotTick) >= (DWORD)g_s.triggerRefractoryMs)) {
        float amp = prof ? prof->shotGain : g_s.shotGain;
        FireShot(cs, now, amp, prof);

        

        {
            float gg = g_s.shotTrigGain > 0.0f ? g_s.shotTrigGain : 1.0f;
            float rtv = (prof ? prof->trigR : g_s.shotTrigR) * gg;
            if (rtv < g_s.shotTrigFloor) rtv = g_s.shotTrigFloor;
            if (rtv > g_s.shotTrigCeil)  rtv = g_s.shotTrigCeil;
            GP_LOG_INFO("haptics: 开枪「%s」-> 扳机 L=%.0f R=%.0f 握把 L=%.0f R=%.0f 时长=%dms",
                        prof ? prof->name : "通用",
                        (prof ? prof->trigL : g_s.shotTrigL) * gg, rtv,
                        (prof ? prof->bodyL : g_s.shotBodyL) * g_s.shotBodyScale * gg,
                        (prof ? prof->bodyR : g_s.shotBodyR) * g_s.shotBodyScale * gg,
                        cs->shotEnvMs);   
        }
    }

    cs->lastAmmo    = st->ammoInClip;
    cs->wasShooting = st->shooting != 0;
}

void GpHapticsGetStatus(uint32_t controller, DWORD now, GpHapticsStatus* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));

    CtrlState* cs = State(controller);
    if (!cs) return;

    if (cs->shotActive) {
        DWORD t = now - cs->shotStart;
        if (t < (DWORD)g_s.shotEnvMs) {
            out->shotActive = 1;
            out->shotRemainMs = (uint16_t)((DWORD)g_s.shotEnvMs - t);
        }
    }
    out->stateValid  = cs->stateValid ? 1 : 0;
    out->weaponGroup = cs->stateGroup;
    out->menuActive  = cs->menuActive;
    out->armed       = cs->armed;
    out->aiming = (uint8_t)(cs->stateValid &&
                  (cs->aiming || cs->padLT >= (BYTE)(g_s.aimTriggerLevel * 255.0f + 0.5f))) ? 1 : 0;

    if (g_s.rideEnable && now < cs->rideUntil) {
        out->rideActive = 1;
        out->ridePeriodMs = (uint16_t)(g_ridePeriod[controller] > 0xFFFF
                                       ? 0xFFFF : g_ridePeriod[controller]);
        float a = cs->rideAmp;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        out->rideAmp = (uint8_t)(a * 255.0f + 0.5f);
    }
}

BOOL GpHapticsActive(uint32_t controller) {
    CtrlState* cs = State(controller);
    if (!cs) return FALSE;
    DWORD now = GetTickCount();
    if (cs->shotActive && (DWORD)(now - cs->shotStart) < (DWORD)g_s.shotEnvMs) return TRUE;
    if (g_s.rideEnable && now < cs->rideUntil) return TRUE;
    if (cs->auxActive) return TRUE;

    





    if (cs->aimActiveNow || cs->bowActiveNow) return TRUE;

    

    if (cs->vehBody > 0.0f) return TRUE;

    return FALSE;
}

void GpTickHaptics(uint32_t controller, DWORD now, BOOL hasGame,
                   BYTE baseL, BYTE baseR, BYTE baseTrigL, BYTE baseTrigR,
                   GpHapticsOut* out) {
    if (!out) return;
    out->leftMotor   = baseL;
    out->rightMotor  = baseR;
    out->leftTrigger  = baseTrigL;
    out->rightTrigger = baseTrigR;

    if (!g_applied || !g_s.enable) return;

    CtrlState* cs = State(controller);
    if (!cs) return;

    cs->aimActiveNow = FALSE;
    cs->bowActiveNow = FALSE;

    float addBodyL = 0.0f, addBodyR = 0.0f, addTrigL = 0.0f, addTrigR = 0.0f;

    
    if (cs->shotActive) {
        

        DWORD envMs = (DWORD)(cs->shotEnvMs > 0 ? cs->shotEnvMs : g_s.shotEnvMs);
        DWORD t = now - cs->shotStart;
        if (t < envMs) {
            float tMs = (float)t;
            

            

            float atkMs = (float)(cs->shotAttackMs > 0 ? cs->shotAttackMs : 8);
            float attack = tMs < atkMs ? (tMs / atkMs) : 1.0f;
            float x = tMs / (float)envMs;
            float decay = powf(1.0f - x, g_s.shotDecayExp);
            float env = attack * decay * cs->shotAmp;

            



            float env255 = env * 255.0f;

            


            

            float g = g_s.shotTrigGain > 0.0f ? g_s.shotTrigGain : 1.0f;
            float rtv = cs->shotTrigR * g;
            if (rtv < g_s.shotTrigFloor) rtv = g_s.shotTrigFloor;
            if (rtv > g_s.shotTrigCeil)  rtv = g_s.shotTrigCeil;

            addTrigL += cs->shotTrigL * env * g;
            addTrigR += rtv * env;
            addBodyL += cs->shotBodyL * env * g_s.shotBodyScale * g;
            addBodyR += cs->shotBodyR * env * g_s.shotBodyScale * g;
        } else {
            cs->shotActive = FALSE;
        }
    }

    




    


    if (g_s.bowDrawGain > 0.0f && cs->stateValid &&
        cs->stateGroup == GPRDR2_GRP_BOW && cs->padRT > 40) {
        cs->bowActiveNow = TRUE;
        if (cs->bowDrawStart == 0) {
            cs->bowDrawStart = now;
            

            GP_LOG_INFO("haptics: 开始拉弓 -> 两侧扳机持续震动（峰值 %.0f，%dms 拉满，握把 %.0f）",
                        (double)(g_s.bowDrawGain * 255.0f), g_s.bowDrawRampMs,
                        (double)(g_s.bowDrawGain * 255.0f * 0.20f));
        }
        DWORD t = now - cs->bowDrawStart;
        float k = (float)t / (float)g_s.bowDrawRampMs;
        if (k > 1.0f) k = 1.0f;

        float amp = g_s.bowDrawGain * k * 255.0f;
        addTrigL += amp;
        addTrigR += amp;
        addBodyL += amp * 0.20f;
        addBodyR += amp * 0.20f;
    } else if (cs->bowDrawStart != 0) {
        cs->bowDrawStart = 0;
        GP_LOG_DEBUG("haptics: 松弦 / 收弓 —— 拉弓震动结束");
    }

    




    if (g_s.rideEnable && g_s.useGameState && cs->stateValid && cs->onMount) {
        float sp = cs->horseSpeed;
        float target = 0.0f;
        BOOL  moving = (sp > g_s.rideSpeedLow);

        if (moving) {
            float k = (sp - g_s.rideSpeedLow) / (g_s.rideSpeedHigh - g_s.rideSpeedLow);
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;

            

            

            target = g_s.rideAmpMin +
                     (g_s.rideAmpMax - g_s.rideAmpMin) * powf(k, g_s.rideCurve);

            
            

            float kp = powf(k, g_s.ridePeriodCurve);
            DWORD period = (DWORD)(g_s.rideMaxPeriodMs +
                                   (g_s.rideMinPeriodMs - g_s.rideMaxPeriodMs) * kp);
            if (period < 120) period = 120;

            
            if (cs->slowMotion && cs->slowStretch > 1.0f)
                period = (DWORD)(period * cs->slowStretch);

            

            int beats;
            switch (cs->horseGait) {
            case 0:  beats = g_s.rideBeatsWalk;   break;   
            case 2:  beats = g_s.rideBeatsGallop; break;   
            case 1:  beats = (sp < g_s.rideTrotMaxSpeed) ? g_s.rideBeatsTrot
                                                         : g_s.rideBeatsCanter;
                     break;
            default: beats = g_s.rideBeats;       break;   
            }
            if (beats < 1) beats = 1;
            DWORD beatMs = period / (DWORD)beats;
            if (beatMs < 45) beatMs = 45;

            cs->rideBeats = beats;
            g_ridePeriod[controller] = beatMs;

            if (cs->rideNextTick == 0 || now >= cs->rideNextTick) {
                cs->rideNextTick = now + beatMs;
                cs->lastPeakTick = now;          
                cs->rideBeat = (cs->rideBeat + 1) % beats;
            }
        } else {
            cs->rideNextTick = 0;
        }

        


        float dt = (float)(now - cs->rideLastTick);
        if (cs->rideLastTick == 0 || dt > 100.0f) dt = 100.0f;
        cs->rideLastTick = now;

        if (g_s.rideFadeMs > 0.5f) {
            float a = dt / g_s.rideFadeMs;
            if (a > 1.0f) a = 1.0f;
            cs->rideLevel += (target - cs->rideLevel) * a;
        } else {
            cs->rideLevel = target;
        }
        

        if (target <= 0.0f && cs->rideLevel < 0.002f) cs->rideLevel = 0.0f;

        cs->rideAmp   = cs->rideLevel;
        if (cs->rideLevel > 0.0f) cs->rideUntil = now + 400;   
    }

    
    if (g_s.rideEnable && now < cs->rideUntil) {
        DWORD period = g_ridePeriod[controller];
        if (period >= 60) {
            








            const float kHoofAttackMs = 4.0f;
            float impactMs = (float)period * 0.45f;
            if (impactMs > 130.0f) impactMs = 130.0f;
            if (impactMs < 45.0f)  impactMs = 45.0f;

            DWORD phase = now - cs->lastPeakTick;
            float env = 0.0f;
            if ((float)phase < impactMs) {
                float x = (float)phase / impactMs;               
                float attack = ((float)phase < kHoofAttackMs)
                               ? ((float)phase / kHoofAttackMs) : 1.0f;
                env = attack * powf(1.0f - x, 1.8f);             
            }

            

            

            float accent = (cs->rideBeats > 0 && cs->rideBeat != 0)
                           ? g_s.rideBeatAccent : 1.0f;

            

            if (g_s.rideReplaceGame && g_s.useGameState && cs->stateValid && cs->onMount) {
                out->leftMotor  = 0;
                out->rightMotor = 0;
            }

            float amp = Clamp01(cs->rideAmp * g_s.rideGain) * env * accent;
            

            float base = Clamp01(g_s.rideBodyBase * g_s.rideGain) *
                         Clamp01(cs->rideAmp / (g_s.rideAmpMin > 0.0f ? g_s.rideAmpMin : 0.1f));
            if (base > 1.0f) base = 1.0f;
            

            addBodyL += base * 255.0f;
            addBodyR += base * 255.0f;
            addBodyL += amp * 255.0f;
            addBodyR += amp * 255.0f;
            addTrigL += amp * g_s.rideTrigGain * 255.0f;
            addTrigR += amp * g_s.rideTrigGain * 255.0f;
        }

        


        if (cs->vehBody > 0.0f) {
            addBodyL += cs->vehBody * 255.0f * 0.6f;
            addBodyR += cs->vehBody * 255.0f * 0.6f;
        }
    } else if (cs->rideLogged) {
        cs->rideLogged = FALSE;
        GP_LOG_INFO("haptics: 步态结束 —— 停止骑乘辅助");
    }

    







    




    DWORD holdMs = cs->ltDown ? (DWORD)(now - cs->ltDownTick) : 0;
    BOOL aimingNow = cs->stateValid && !cs->menuActive && !cs->uiOverlay &&
                     (cs->aiming ||
                      (cs->armed && cs->ltDown && holdMs >= (DWORD)g_s.aimHoldMs));
    if (g_s.aimEnable && g_s.useGameState && aimingNow) {
        const GpWeaponProfile* prof = FindProfile(cs->stateGroup, cs->lastWeapon);
        if (prof && (prof->aimTrig > 0.0f || prof->aimBody > 0.0f)) {
            

            



            DWORD origin = cs->ltDown ? cs->ltDownTick : now;
            if (cs->aimStart != origin) cs->aimStart = origin;
            

            float ramp = 1.0f;
            if (g_s.aimRampGain > 0.0f) {
                float t = (float)(now - cs->aimStart) / g_s.aimRampMs;
                if (t > 1.0f) t = 1.0f;
                if (t < 0.0f) t = 0.0f;
                ramp = 1.0f + powf(t, g_s.aimRampCurve) * g_s.aimRampGain;
            }

            


            float breath = 1.0f;
            if (g_s.aimBreathHz > 0.01f && prof->aimWobble > 0.0f) {
                float secs = (float)now / 1000.0f;
                breath = 1.0f - prof->aimWobble * 0.5f *
                                (1.0f - cosf(6.2831853f * g_s.aimBreathHz * secs));
            }
            cs->aimActiveNow = TRUE;
            float ts = g_s.aimTrigScale > 0.0f ? g_s.aimTrigScale : 1.0f;
            float bs = g_s.aimBodyScale > 0.0f ? g_s.aimBodyScale : 1.0f;
            addTrigL += prof->aimTrig * 255.0f * ramp * ts;
            
            if (g_s.aimBothTriggers)
                addTrigR += prof->aimTrig * 255.0f * ramp * ts;
            

            addBodyL += prof->aimBody * 255.0f * breath * bs;
            addBodyR += prof->aimBody * 255.0f * breath * bs;
        }
    } else {
        
        cs->aimStart = 0;
    }

    
    if (cs->auxActive) {
        DWORD t = now - cs->auxStart;
        if (t < (DWORD)cs->auxEnvMs) {
            float x = (float)t / (float)cs->auxEnvMs;
            float env = powf(1.0f - x, 1.5f) * cs->auxAmp * 255.0f;
            if (cs->auxSide == 1)      { addTrigL += env; addBodyL += env * 0.25f; }
            else if (cs->auxSide == 2) { addTrigL += env; addTrigR += env;
                                         addBodyL += env * 0.25f; addBodyR += env * 0.25f; }
            else                       { addTrigR += env; addBodyR += env * 0.25f; }
        } else {
            cs->auxActive = FALSE;
        }
    }

    
    




    BOOL takeOver = (cs->shotActive && g_s.shotReplaceGame);

    if (takeOver) {
        if (g_s.driveLeftMotor)    out->leftMotor    = ClampByte(addBodyL);
        if (g_s.driveRightMotor)   out->rightMotor   = ClampByte(addBodyR);
        if (g_s.driveLeftTrigger)  out->leftTrigger  = ClampByte(addTrigL);
        if (g_s.driveRightTrigger) out->rightTrigger = ClampByte(addTrigR);
        return;
    }

    if (g_s.driveLeftMotor)
        out->leftMotor    = ClampByte((float)out->leftMotor + addBodyL);
    if (g_s.driveRightMotor)
        out->rightMotor   = ClampByte((float)out->rightMotor + addBodyR);
    if (g_s.driveLeftTrigger)
        out->leftTrigger  = ClampByte((float)out->leftTrigger + addTrigL);
    if (g_s.driveRightTrigger)
        out->rightTrigger = ClampByte((float)out->rightTrigger + addTrigR);

    (void)hasGame;   
}

void GpFireEffect(uint32_t controller, int effectId, float gain, int envMs, int side) {
    CtrlState* cs = State(controller);
    if (!cs) return;
    if (gain <= 0.0f) return;                 
    (void)effectId;                           
    FireAux(cs, GetTickCount(), gain, envMs, side);
}

void GpResetHaptics(void) {
    memset(g_c, 0, sizeof(g_c));
    memset(g_ridePeriod, 0, sizeof(g_ridePeriod));
}
