






















#ifndef GP_VIBIPC_H
#define GP_VIBIPC_H

#include <stdint.h>
#include <windows.h>
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif



#define GPVIB_MAGIC          0x47564231u
#define GPVIB_VERSION        1u

#define GPVIB_RING_SIZE      512u
#define GPVIB_RING_MASK      (GPVIB_RING_SIZE - 1u)
#define GPVIB_DATA_BYTES     64u
#define GPVIB_NOTE_BYTES     48u


#define GPVIB_HEARTBEAT_TIMEOUT_MS  2000u

#define GPVIB_NAME_SHARED    L"Local\\GpVibSend_v1"
#define GPVIB_NAME_EVENT     L"Local\\GpVibSend_v1_evt"


typedef enum GpVibLane {
    GPVIB_LANE_UNKNOWN  = 0,
    GPVIB_LANE_XINPUT   = 1,   
    GPVIB_LANE_WGI      = 2,   
    GPVIB_LANE_MS_HID   = 3,   
    GPVIB_LANE_SONY     = 4,   
    GPVIB_LANE_SONY_TRIG= 5,   
    GPVIB_LANE_NINTENDO = 6,   
    GPVIB_LANE_PROBE    = 7,   
    GPVIB_LANE_CUSTOM   = 8,   
    GPVIB_LANE_TRIGGER  = 9    
} GpVibLane;

static __inline const char* gpvib_lane_name(uint8_t lane) {
    switch (lane) {
    case GPVIB_LANE_XINPUT:    return "XInput";
    case GPVIB_LANE_WGI:       return "WGI";
    case GPVIB_LANE_MS_HID:    return "微软9字节";
    case GPVIB_LANE_SONY:      return "索尼";
    case GPVIB_LANE_SONY_TRIG: return "索尼扳机";
    case GPVIB_LANE_NINTENDO:  return "任天堂0x10";
    case GPVIB_LANE_PROBE:     return "摸索";
    case GPVIB_LANE_CUSTOM:    return "自定义";
    case GPVIB_LANE_TRIGGER:   return "扳机";
    default:                   return "未知";
    }
}




typedef struct GpVibFrame {
    uint64_t seq;          
    uint64_t tickMs;       
    uint32_t procPid;      
    uint16_t devVid;       
    uint16_t devPid;
    int16_t  reportId;     
    uint8_t  lane;         
    uint8_t  target;       
    uint8_t  len;          
    uint8_t  reserved;
    uint8_t  data[GPVIB_DATA_BYTES];
    char     note[GPVIB_NOTE_BYTES];   
} GpVibFrame;

typedef struct GpVibSlot {
    


    volatile uint64_t seq;
    GpVibFrame frame;
} GpVibSlot;

typedef struct GpVibShared {
    
    uint32_t magic;
    uint32_t version;
    uint32_t size;                    
    volatile uint32_t producerHeartbeat;   
    volatile uint32_t statPublished;       
    volatile uint32_t reservedCtl[3];

    
    

    volatile int64_t claim;
    volatile uint32_t statDropped;         
    volatile uint32_t reservedRing[3];
    GpVibSlot slots[GPVIB_RING_SIZE];
} GpVibShared;









static __inline uint64_t gpvib_push(GpVibShared* sh, const GpVibFrame* f) {
    if (!sh) return 0;
    int64_t claim = _InterlockedIncrement64(&sh->claim) - 1;
    uint32_t idx = (uint32_t)(claim & (int64_t)GPVIB_RING_MASK);
    GpVibSlot* slot = &sh->slots[idx];

    uint64_t seq = (uint64_t)claim + 1;
    slot->seq = 0;                    
    _ReadWriteBarrier();

    slot->frame = *f;
    slot->frame.seq = seq;

    _ReadWriteBarrier();
    slot->seq = seq;                  

    _InterlockedIncrement((volatile long*)&sh->statPublished);
    return seq;
}









static __inline int gpvib_drain(const GpVibShared* sh, uint64_t* lastSeq,
                                GpVibFrame* out, int maxOut, uint32_t* dropped) {
    if (!sh || !lastSeq || !out || maxOut <= 0) return 0;

    int64_t claim = sh->claim;
    if (claim <= 0) return 0;

    uint64_t newest = (uint64_t)claim;
    int got = 0;

    while (*lastSeq < newest && got < maxOut) {
        uint64_t want = *lastSeq + 1;
        uint32_t idx = (uint32_t)((want - 1) & (uint64_t)GPVIB_RING_MASK);
        const GpVibSlot* slot = &sh->slots[idx];

        if (slot->seq == want) {
            out[got++] = slot->frame;
        } else if (dropped) {
            ++(*dropped);         
        }
        *lastSeq = want;
    }
    return got;
}

#ifdef __cplusplus
}  
#endif

#endif 
