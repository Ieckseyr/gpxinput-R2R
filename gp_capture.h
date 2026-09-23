// capture
#ifndef GP_CAPTURE_H
#define GP_CAPTURE_H

#include <windows.h>
#include "gp_ipc.h"

namespace gpcapture {



void Init(const wchar_t* path);





void Frame(const GpFrame* in, const GpFrame* out, BYTE padLT, BYTE padRT);



void Tick(void);


void Shutdown(void);


BOOL Enabled(void);

}  

#endif 
