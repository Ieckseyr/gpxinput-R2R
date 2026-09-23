// wgi
#ifndef GP_WGI_H
#define GP_WGI_H

#include <windows.h>

namespace gpwgi {



void Tick(void);


int Count(void);


BOOL Set(BYTE leftMotor, BYTE rightMotor, BYTE leftTrigger, BYTE rightTrigger);


long SentCount(void);

}  

#endif 
