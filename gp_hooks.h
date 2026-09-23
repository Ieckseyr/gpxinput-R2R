// hooks
#ifndef GP_HOOKS_H
#define GP_HOOKS_H

#include <windows.h>
#include "gp_real.h"

namespace gphooks {




bool Install(void);


void Uninstall(void);


bool Installed(void);


GpFnSetState Trampoline(void);

}  

#endif 
