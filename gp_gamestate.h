// state
#ifndef GP_GAMESTATE_H
#define GP_GAMESTATE_H

#include <windows.h>
#include "gp_rdr2_state.h"

namespace gpgame {


void Attach(void);
void Detach(void);



bool Read(GpRdr2State* out);


bool EverHad(void);

}  

#endif 
