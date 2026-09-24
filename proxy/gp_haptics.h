// haptics
#ifndef GP_HAPTICS_H
#define GP_HAPTICS_H

#include <windows.h>
#include <stdint.h>

#include "gp_rdr2_state.h"



#define GP_WEAPON_SLOTS 8    
#define GP_GUN_SLOTS   96    






typedef struct GpWeaponProfile {
    uint32_t hash;         
    float    shotGain;     
    int      shotEnvMs;    
    float    shotBodyKick; 
    float    aimTrig;      
    float    aimBody;      
    float    aimWobble;    
    






    float trigL;         
    float trigR;         
    float bodyL;         
    float bodyR;         
    int   attackMs;      
    char     name[24];     
} GpWeaponProfile;

struct GpHapticsSettings {
    



    BOOL  enable;

    








    BOOL  driveLeftMotor;      
    BOOL  driveRightMotor;     
    BOOL  driveLeftTrigger;    
    BOOL  driveRightTrigger;   

    
    



    BOOL  shotReplaceGame;

    

    BOOL  shotFromTrigger;
    float triggerPressThresh;  
    




    float triggerReleaseHyst;
    

    int   triggerRefractoryMs;

    




    BOOL  shotFromRumble;

    float shotRiseThresh;      
    float shotGain;            
    int   shotEnvMs;           
    int   shotRefractoryMs;    
    int   shotSide;            
    float shotBodyKick;      

    
    float shotTrigL, shotTrigR, shotBodyL, shotBodyR;
    int   shotAttackMs;
    float shotBodyScale;
    float shotDecayExp;

    

    float shotEnvScale;

    



    float shotTrigGain;
    float shotTrigFloor;
    float shotTrigCeil;        
    





    float rideShotBoost;

    
    

    BOOL  rideEnable;
    float ridePeakThresh;      
    float rideGain;            
    float rideTrigGain;        
    int   rideMinPeriodMs;     
    int   rideMaxPeriodMs;     
    float ridePeriodTol;       
    int   rideHoldMs;          

    


    float rideSpeedLow;
    float rideSpeedHigh;
    




    float rideCurve;

    

    

    float rideAmpMax;

    



    






    BOOL  rideReplaceGame;

    




    




    float slowMoStretch;     
    float slowMoWheel;
    float slowMoDeadEye;
    float slowMoEagle;

    
    float mountJumpGain;

    

    float mountLandGain;

    



    float spookGain;

    


    float mountLandRise;

    


    BOOL  wagonEnable;
    float wagonGain;
    int   wagonMinPeriodMs;    
    int   wagonMaxPeriodMs;    
    float wagonSpeedLow;       
    float wagonSpeedHigh;      
    float wagonTrigGain;       
    float wagonBodyBase;       

    
    BOOL  trainRideEnable;
    float trainRideGain;
    int   trainRideMinPeriodMs;
    int   trainRideMaxPeriodMs;
    float trainRideTrigGain;
    float trainRideBodyBase;

    

    BOOL  trainPassEnable;
    float trainPassGain;
    float trainPassRadius;      
    float trainPassApproachRef; 
    int   trainPassEnvMs;       
    int   spookEnvMs;
    float spookAccel;
    int   mountLandEnvMs;
    float mountLandHeight;    

    int   mountJumpEnvMs;
    
    BOOL  rideReplaceGameDummy;

    float rideAmpMin;

    

    float rideBodyBase;

    

    float rideFadeMs;

    

    int   rideBeatsWalk;
    int   rideBeatsTrot;
    int   rideBeatsCanter;
    int   rideBeatsGallop;
    

    float rideTrotMaxSpeed;
    

    float ridePeriodCurve;

    int   rideBeats;          
    float rideBeatAccent;      

    
    

    float trigToBody;

    
    

    BOOL  useGameState;

    



    BOOL  aimEnable;

    

    float aimTrigScale;
    float aimBodyScale;

    




    BOOL  aimBothTriggers;

    
    float aimBreathHz;

    


    float aimTriggerLevel;

    
    int   aimHoldMs;

    

    float aimRampMs;
    float aimRampGain;
    

    float aimRampCurve;

    
    float tickGain;
    int   tickEnvMs;

    




    float landGain;        
    int   landEnvMs;
    float injuryGain;      
    int   injuryEnvMs;
    float lowHealthHeartbeat;  
    float deadeyeGain;     
    int   deadeyeEnvMs;

    


    float bowDrawGain;
    int   bowDrawRampMs;      

    

    float reloadGain;
    int   reloadEnvMs;
    int   reloadSide;

    
    float drawGain;
    int   drawEnvMs;

    




    BOOL  ltPressEnable;
    float ltPressGain;
    int   ltPressEnvMs;

    





    GpWeaponProfile weapon[GP_WEAPON_SLOTS];
    int             weaponCount;
    GpWeaponProfile gun[GP_GUN_SLOTS];
    int             gunCount;
};


struct GpHapticsOut {
    BYTE leftMotor;
    BYTE rightMotor;
    BYTE leftTrigger;
    BYTE rightTrigger;
};


void GpDefaultHapticsSettings(GpHapticsSettings* s);


void GpApplyHapticsSettings(const GpHapticsSettings& s);



void GpOnGameFrame(uint32_t controller, DWORD tick, BYTE bodyL, BYTE bodyR);







void GpOnPadInput(uint32_t controller, DWORD now, BYTE leftTrigger, BYTE rightTrigger);



void GpOnGameState(uint32_t controller, DWORD now, BOOL valid, const GpRdr2State* st);



void GpTickHaptics(uint32_t controller, DWORD now, BOOL hasGame,
                   BYTE baseL, BYTE baseR, BYTE baseTrigL, BYTE baseTrigR,
                   GpHapticsOut* out);



BOOL GpHapticsActive(uint32_t controller);


typedef struct GpHapticsStatus {
    uint8_t  aimActive;      
    uint8_t  bowActive;      
    uint8_t  shotActive;
    uint16_t shotRemainMs;
    uint8_t  rideActive;
    uint16_t ridePeriodMs;
    uint8_t  rideAmp;        
    uint8_t  stateValid;     
    uint8_t  aiming;         
    uint8_t  menuActive;     
    uint8_t  armed;          
    uint32_t weaponGroup;    
} GpHapticsStatus;

void GpHapticsGetStatus(uint32_t controller, DWORD now, GpHapticsStatus* out);











enum {
    GP_FX_DRAW   = 1,   
    GP_FX_TICK   = 2,   
    GP_FX_RELOAD = 3,   
    GP_FX_LT     = 4,   
};






void GpFireEffect(uint32_t controller, int effectId, float gain, int envMs, int side);


void GpResetHaptics(void);

#endif 
