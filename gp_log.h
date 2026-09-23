// log
#ifndef GP_LOG_H
#define GP_LOG_H

#include <windows.h>

namespace gplog {

enum Level {
    LVL_OFF   = 0,
    LVL_ERROR = 1,
    LVL_INFO  = 2,
    LVL_DEBUG = 3,
    LVL_TRACE = 4,
};



void Init(const wchar_t* path);


void Shutdown(void);



const char* PathUtf8(void);

void SetLevel(Level lvl);
Level GetLevel(void);









const char* W(const wchar_t* s);


void Write(Level lvl, const char* fmt, ...);


void HexDump(Level lvl, const char* tag, const void* data, unsigned int len);

}  

#define GP_LOG_ERROR(...) gplog::Write(gplog::LVL_ERROR, __VA_ARGS__)
#define GP_LOG_INFO(...)  gplog::Write(gplog::LVL_INFO,  __VA_ARGS__)
#define GP_LOG_DEBUG(...) gplog::Write(gplog::LVL_DEBUG, __VA_ARGS__)
#define GP_LOG_TRACE(...) gplog::Write(gplog::LVL_TRACE, __VA_ARGS__)

#endif 
