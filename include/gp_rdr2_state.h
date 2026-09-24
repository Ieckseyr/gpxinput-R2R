// state
#ifndef GP_RDR2_STATE_H
#define GP_RDR2_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPRDR2_MAGIC   0x52533231u   
#define GPRDR2_VERSION 9u
#define GPRDR2_NAME    L"Local\\GpRdr2State_v1"


#define GPRDR2_TIMEOUT_MS 1000u



#define GPRDR2_GRP_PISTOL      0x18D5FA97u
#define GPRDR2_GRP_REVOLVER    0xBE5B8969u
#define GPRDR2_GRP_REPEATER    0xDC8FB3E9u
#define GPRDR2_GRP_RIFLE       0x39D5C192u
#define GPRDR2_GRP_SHOTGUN     0x33431399u
#define GPRDR2_GRP_SNIPER      0xB7BBD827u
#define GPRDR2_GRP_MELEE       0xD49321D4u
#define GPRDR2_GRP_BOW         0xB5FD67CDu
#define GPRDR2_GRP_THROWN      0x5C4C5883u
#define GPRDR2_GRP_LASSO       0x126210C3u
#define GPRDR2_GRP_KIT         0x6D8DC58Fu
#define GPRDR2_GRP_FIST        0xC8D8FD45u
#define GPRDR2_GRP_LANTERN     0x37F7AAB0u

typedef struct GpRdr2State {
    

    uint32_t magic;
    uint32_t version;

    
    volatile uint32_t seq;

    uint32_t tickMs;          
    uint32_t frame;           
    uint32_t writerPid;

    
    uint32_t weaponHash;      
    uint32_t weaponGroup;     
    int32_t  ammoInClip;      
    uint32_t timeSinceShot;   

    
    uint8_t  shooting;        
    uint8_t  aiming;          
    uint8_t  reloading;       
    uint8_t  onFoot;
    uint8_t  menuActive;      
    uint8_t  armed;           
    uint8_t  onMount;         
    uint8_t  inVehicle;
    uint8_t  pad0;

    




    uint8_t  horseGait;
    uint8_t  mountJumping;    
    uint8_t  mountFalling;    
    float    mountHeight;     
    uint8_t  mountHurt;       
    float    horseAccel;      

    

    uint8_t  inTrain;         
    uint32_t vehicleModel;    
    float    vehicleSpeed;    
    uint8_t  trainNearby;     
    float    trainDist;       
    float    trainApproach;       

    



    uint8_t  jumping;         
    uint8_t  falling;         
    uint8_t  climbing;        
    uint8_t  vaulting;        
    uint8_t  swimming;        
    uint8_t  inCover;         
    uint8_t  playerGait;      
    uint8_t  pad1;

    


    float    timeScale;

    
    uint8_t  slowMotion;      


    uint8_t  uiOverlay;       

    uint8_t  grounded;        
    uint8_t  deadOrDying;     
    uint8_t  prone;           

    int      health;          
    int      maxHealth;       

    
    float    horseSpeed;      
    float    playerSpeed;

    uint32_t mountHash;       
    uint32_t playerPed;       

    uint32_t reserved[4];
} GpRdr2State;

#ifdef __cplusplus
}  
#endif

#endif 
