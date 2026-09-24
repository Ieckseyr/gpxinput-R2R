// ipc
#ifndef GP_IPC_H
#define GP_IPC_H

#include <stdint.h>
#include <windows.h>
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif







#define GPIPC_MAGIC            0x47504932u
#define GPIPC_VERSION          2u

#define GPIPC_RING_SIZE        256u
#define GPIPC_RING_MASK        (GPIPC_RING_SIZE - 1u)
#define GPIPC_MAX_CONTROLLERS  4u



#define GPIPC_HEARTBEAT_TIMEOUT_MS  1000u
#define GPIPC_HEARTBEAT_PERIOD_MS   250u

#define GPIPC_NAME_CTL         L"Local\\GpXInput_v2_ctl"
#define GPIPC_NAME_RING        L"Local\\GpXInput_v2_ring"
#define GPIPC_NAME_FRAME_EVT   L"Local\\GpXInput_v2_frame_evt"
#define GPIPC_NAME_RESULT_EVT  L"Local\\GpXInput_v2_result_evt"







typedef enum GpSource {
    GP_SRC_UNKNOWN     = 0,
    GP_SRC_XINPUT      = 1,  
    GP_SRC_STEAM_IOCTL = 2,  
    GP_SRC_MS_IOCTL    = 3,  
    GP_SRC_HID_WRITE   = 4,  
    GP_SRC_PROCESSOR   = 5,  
    

    GP_SRC_WGI         = 6
} GpSource;


typedef enum GpPolicy {
    
    GP_POLICY_PASSTHROUGH = 0,

    

    GP_POLICY_REWRITE     = 1,

    

    GP_POLICY_REPLACE     = 2
} GpPolicy;



typedef enum GpOutput {
    GP_OUT_AUTO   = 0,
    GP_OUT_XINPUT = 1,
    GP_OUT_HID    = 2
} GpOutput;


#define GP_FRAME_F_FROM_GAME   0x01u  
#define GP_FRAME_F_BLOCKED     0x02u  
#define GP_FRAME_F_PROCESSED   0x04u  
#define GP_FRAME_F_FALLBACK    0x08u  
#define GP_FRAME_F_FULL_TICK   0x10u  


typedef enum GpConn {
    GP_CONN_USB_24G    = 0,  
    GP_CONN_BLUETOOTH  = 1   
} GpConn;








#pragma pack(push, 8)
typedef struct GpFrame {
    uint64_t seq;            
    uint32_t tickMs;         
    uint32_t pid;            
    uint8_t  controller;     
    uint8_t  source;         
    uint8_t  flags;          
    uint8_t  conn;           
    float    leftMotor;      
    float    rightMotor;
    float    leftTrigger;
    float    rightTrigger;
    uint8_t  rawLeftMotor;   
    uint8_t  rawRightMotor;
    uint8_t  rawLeftTrigger;
    uint8_t  rawRightTrigger;
} GpFrame;                   
#pragma pack(pop)





typedef struct GpRing {
    

    volatile int64_t claim;

    

    GpFrame frames[GPIPC_RING_SIZE];
} GpRing;








static __inline uint64_t gpipc_ring_push(GpRing* ring, GpFrame* frame) {
    int64_t claim = _InterlockedIncrement64(&ring->claim) - 1;
    uint32_t slot = (uint32_t)(claim & (int64_t)GPIPC_RING_MASK);
    GpFrame* dst = &ring->frames[slot];

    uint64_t seq = (uint64_t)(claim * 2 + 2);
    frame->seq = seq;
    dst->seq = (uint64_t)(claim * 2 + 1);   
    _ReadWriteBarrier();

    dst->tickMs        = frame->tickMs;
    dst->pid           = frame->pid;
    dst->controller    = frame->controller;
    dst->source        = frame->source;
    dst->flags         = frame->flags;
    dst->conn          = frame->conn;
    dst->leftMotor     = frame->leftMotor;
    dst->rightMotor    = frame->rightMotor;
    dst->leftTrigger   = frame->leftTrigger;
    dst->rightTrigger  = frame->rightTrigger;
    dst->rawLeftMotor  = frame->rawLeftMotor;
    dst->rawRightMotor = frame->rawRightMotor;
    dst->rawLeftTrigger  = frame->rawLeftTrigger;
    dst->rawRightTrigger = frame->rawRightTrigger;

    _ReadWriteBarrier();
    dst->seq = seq;                         
    return seq;
}



static __inline int gpipc_ring_peek(const GpRing* ring, uint32_t slot, GpFrame* out) {
    const GpFrame* src = &ring->frames[slot & GPIPC_RING_MASK];
    uint64_t seq0 = src->seq;
    if (seq0 == 0) return 0;            
    if (seq0 & 1u) return 0;            

    out->seq          = seq0;
    out->tickMs       = src->tickMs;
    out->pid          = src->pid;
    out->controller   = src->controller;
    out->source       = src->source;
    out->flags        = src->flags;
    out->conn         = src->conn;
    out->leftMotor    = src->leftMotor;
    out->rightMotor   = src->rightMotor;
    out->leftTrigger  = src->leftTrigger;
    out->rightTrigger = src->rightTrigger;
    out->rawLeftMotor   = src->rawLeftMotor;
    out->rawRightMotor  = src->rawRightMotor;
    out->rawLeftTrigger  = src->rawLeftTrigger;
    out->rawRightTrigger = src->rawRightTrigger;

    _ReadWriteBarrier();
    if (src->seq != seq0) return 0;     
    return 1;
}





typedef struct GpControl {
    uint32_t magic;
    uint32_t version;
    uint32_t size;

    

    volatile uint32_t producerHeartbeat;   
    volatile uint32_t consumerHeartbeat;   
    volatile uint32_t consumerPid;
    volatile uint32_t producerCount;       

    
    volatile uint32_t enabled;             
    volatile uint32_t policy;              
    volatile uint32_t output;              
    volatile uint32_t timeoutMs;           

    
    volatile uint32_t statFramesIn;        
    volatile uint32_t statFramesOut;       
    volatile uint32_t statBlocked;         
    volatile uint32_t statTimeouts;        
    volatile uint32_t statRingDrops;       

    volatile uint32_t statProcessed;       

    

    volatile uint32_t resultSeq[GPIPC_MAX_CONTROLLERS];   
    GpFrame result[GPIPC_MAX_CONTROLLERS];
} GpControl;


static __inline void gpipc_result_write(GpControl* ctl, uint32_t controller, const GpFrame* f) {
    if (controller >= GPIPC_MAX_CONTROLLERS) return;
    _InterlockedIncrement((volatile long*)&ctl->resultSeq[controller]);   
    _ReadWriteBarrier();
    ctl->result[controller] = *f;
    _ReadWriteBarrier();
    _InterlockedIncrement((volatile long*)&ctl->resultSeq[controller]);   
}


static __inline int gpipc_result_read(const GpControl* ctl, uint32_t controller, GpFrame* out) {
    if (controller >= GPIPC_MAX_CONTROLLERS) return 0;
    uint32_t s0 = ctl->resultSeq[controller];
    if (s0 == 0) return 0;
    if (s0 & 1u) return 0;
    *out = ctl->result[controller];
    _ReadWriteBarrier();
    if (ctl->resultSeq[controller] != s0) return 0;
    return 1;
}



static __inline int gpipc_consumer_alive(const GpControl* ctl, uint32_t nowMs) {
    uint32_t beat = ctl->consumerHeartbeat;
    if (beat == 0) return 0;
    return (uint32_t)(nowMs - beat) < GPIPC_HEARTBEAT_TIMEOUT_MS;
}


static __inline int gpipc_same_payload(const GpFrame* a, const GpFrame* b) {
    return a->controller == b->controller
        && a->rawLeftMotor == b->rawLeftMotor
        && a->rawRightMotor == b->rawRightMotor
        && a->rawLeftTrigger == b->rawLeftTrigger
        && a->rawRightTrigger == b->rawRightTrigger;
}

#ifdef __cplusplus
}  
#endif

#endif 
