








#ifndef GP_IPC_CLIENT_H
#define GP_IPC_CLIENT_H

#include <windows.h>
#include "gp_ipc.h"

namespace gpshm {



bool Attach(void);

void Detach(void);

bool IsAttached(void);


GpControl* Control(void);






uint64_t Publish(GpFrame* frame);


void BeatProducer(void);



void Release(void);


bool ConsumerAlive(void);



GpPolicy EffectivePolicy(void);


GpOutput PreferredOutput(void);


DWORD ResultTimeoutMs(void);


bool WaitResult(DWORD timeoutMs);




bool ReadResult(uint32_t controller, uint64_t frameSeq, GpFrame* out);

}  

#endif 
