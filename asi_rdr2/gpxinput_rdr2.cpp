// asi
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "sh_rdr2.h"
#include "rdr2_natives.h"
#include "gp_rdr2_state.h"

namespace {



volatile LONG g_step    = 0;
BOOL          g_faulted = FALSE;
volatile LONG g_frames  = 0;   
volatile LONG g_ammoFaults = 0;   
int g_ammoClip = 0;                
unsigned long g_excCode = 0;

BOOL g_tickSeen      = FALSE;
BOOL g_outsideWorld  = FALSE;


HMODULE       g_self       = nullptr;
HANDLE        g_mapping    = nullptr;
GpRdr2State*  g_state      = nullptr;
uint32_t      g_frame      = 0;
volatile LONG g_registered = 0;      
volatile LONG g_disabled   = 0;
BOOL          g_checked    = FALSE;  
int           g_bootFrames = 0;
char          g_logPath[MAX_PATH] = {0};

void Log(const char* fmt, ...) {
    if (!g_logPath[0]) {
        GetModuleFileNameA(nullptr, g_logPath, MAX_PATH);
        char* slash = strrchr(g_logPath, '\\');
        if (slash) slash[1] = 0;
        strncat_s(g_logPath, MAX_PATH, "gpxinput_rdr2.log", _TRUNCATE);
    }
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

bool OpenState(void) {
    if (g_state) return true;

    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, GPRDR2_NAME);
    bool created = false;
    if (!h) {
        h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               sizeof(GpRdr2State), GPRDR2_NAME);
        if (h && GetLastError() != ERROR_ALREADY_EXISTS) created = true;
    }
    if (!h) {
        Log("创建/打开状态段失败 (err=%lu)", GetLastError());
        return false;
    }
    void* p = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!p) {
        Log("映射状态段失败 (err=%lu)", GetLastError());
        CloseHandle(h);
        return false;
    }
    g_mapping = h;
    g_state = (GpRdr2State*)p;

    if (created) {
        memset(g_state, 0, sizeof(GpRdr2State));
        g_state->magic   = GPRDR2_MAGIC;
        g_state->version = GPRDR2_VERSION;
        g_state->writerPid = GetCurrentProcessId();
    }
    Log("状态段就绪（%s）", created ? "本进程创建" : "接手已有");
    return true;
}



uint32_t CurrentWeapon(int ped) {
    uint32_t hash = 0;
    



    sh::nativeInit(N_GET_CURRENT_PED_WEAPON);
    sh::nativePush64((uint64_t)(int64_t)ped);
    sh::nativePush64((uint64_t)(uintptr_t)&hash);
    sh::nativePush64(0);            
    sh::nativePush64(0);            
    sh::nativePush64(0);            
    sh::nativeCall();
    return hash;
}






void ShowFeedOnce(const char* text)
{
    __try {
        sh::nativeInit(N_CREATE_STRING);
        sh::nativePush64(10);
        sh::nativePush64((uint64_t)(uintptr_t)"LITERAL_STRING");
        sh::nativePush64((uint64_t)(uintptr_t)text);
        uint64_t* r = sh::nativeCall();
        const char* packed = r ? (const char*)*r : nullptr;
        if (!packed) return;

        sh::nativeInit(N_UILOG_SET_CACHED_OBJECTIVE);
        sh::nativePush64((uint64_t)(uintptr_t)packed);
        sh::nativeCall();

        sh::nativeInit(N_UILOG_PRINT_CACHED_OBJECTIVE);
        sh::nativeCall();

        sh::nativeInit(N_UILOG_CLEAR_CACHED_OBJECTIVE);
        sh::nativeCall();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        
    }
}

const char* GroupName(uint32_t h) {
    switch (h) {
    case GPRDR2_GRP_PISTOL:   return "Pistol";
    case GPRDR2_GRP_REVOLVER: return "Revolver";
    case GPRDR2_GRP_REPEATER: return "Repeater";
    case GPRDR2_GRP_RIFLE:    return "Rifle";
    case GPRDR2_GRP_SHOTGUN:  return "Shotgun";
    case GPRDR2_GRP_SNIPER:   return "Sniper";
    case GPRDR2_GRP_BOW:      return "Bow";
    case GPRDR2_GRP_MELEE:    return "Melee";
    case GPRDR2_GRP_THROWN:   return "Thrown";
    case GPRDR2_GRP_LASSO:    return "Lasso";
    case 0:                   return "空手/无";
    default:                  return "未知组";
    }
}




























#define GP_UNARMED_HASH 0xA2719263u

BOOL WeaponHashSafe(uint32_t w) {
    static uint32_t sPrev = 0;
    BOOL stable = (w != 0) && (w != GP_UNARMED_HASH) && (w == sPrev);
    sPrev = w;
    return stable;
}

int ReadAmmoTotal(int ped, uint32_t weapon) {
    int total = -1;
    if (!WeaponHashSafe(weapon)) return -1;   
    __try {
        total = (int)rdr2_call2(N_GET_AMMO_IN_PED_WEAPON, (uint64_t)(int64_t)ped,
                                (uint64_t)weapon);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        total = -1;
    }
    return total;                       
}

BOOL GroupHasClip(uint32_t group) {
    switch (group) {
    case GPRDR2_GRP_PISTOL:
    case GPRDR2_GRP_REVOLVER:
    case GPRDR2_GRP_REPEATER:
    case GPRDR2_GRP_RIFLE:
    case GPRDR2_GRP_SHOTGUN:
    case GPRDR2_GRP_SNIPER:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL ReadAmmo(int ped, uint32_t weapon, uint32_t group, int* out) {
    if (!GroupHasClip(group)) return FALSE;   
    if (!WeaponHashSafe(weapon)) return FALSE; 
    if (g_ammoFaults >= 3) return FALSE;

    


    BOOL ok = TRUE;
    __try {
        int ammo = 0;
        rdr2_call3(N_GET_AMMO_IN_CLIP, (uint64_t)(int64_t)ped,
                   (uint64_t)weapon, (uint64_t)(uintptr_t)&ammo);
        *out = ammo;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = FALSE;
    }
    if (ok) return TRUE;

    LONG n = InterlockedIncrement(&g_ammoFaults);
    if (n <= 3) {
        Log("弹匣 native 调用异常（第 %ld 次）—— 本次跳过；连续 3 次后停用该项", n);
    } else {
        Log("弹匣 native 已停用：开枪判定退回 shooting 上升沿");
    }
    return FALSE;
}

void Tick(void) {
    






    for (;;) {
        if (InterlockedCompareExchange(&g_disabled, 1, 1) == 1) {
            sh::scriptWait(500);
            continue;
        }

        

        if (!g_tickSeen) {
            g_tickSeen = TRUE;
            Log("脚本线程已启动（Tick 开始被调用）");
        }

        


        __try {
            g_step = 1;
            int ped = (int)rdr2_call0(N_PLAYER_PED_ID);

            

            if (!g_checked && ped != 0) {
                g_checked = TRUE;
                Log("自检通过：playerPed=%d，开始发布游戏状态", ped);
            }

            




            if (ped == 0) {
                if (!g_outsideWorld) {
                    g_outsideWorld = TRUE;
                    Log("玩家 ped=0（主菜单/加载中）—— 进世界后开始发布状态");
                }
                sh::scriptWait(0);
                continue;
            }
            if (g_outsideWorld) {
                g_outsideWorld = FALSE;
                Log("进入世界：ped=%d", ped);
            }

            
            {
                static BOOL sFeedShown = FALSE;
                if (!sFeedShown) {
                    sFeedShown = TRUE;
                    ShowFeedOnce("gpxinput 手柄强震 mod | 作者: 伊希娅 | 定制可加 Q 3529832433");
                    Log("已在信息栏弹出 mod 信息");
                }
            }

            


            g_step = 2;
            uint32_t weapon    = CurrentWeapon(ped);
        


        {
            static uint32_t lastAmmoWeapon = 0;
            if (weapon != lastAmmoWeapon) {
                lastAmmoWeapon = weapon;
                g_ammoFaults = 0;
            }
        }
            uint32_t group     = 0;
            int      ammo      = 0;
            if (weapon) {
                g_step = 3;
                group = (uint32_t)rdr2_call1(N_GET_WEAPONTYPE_GROUP, weapon);
                g_step = 4;
                int total = ReadAmmoTotal(ped, weapon);
                if (total >= 0) {
                    ammo = total;
                } else {
                    ammo = 0;
                }
                g_ammoClip = 0;
                ReadAmmo(ped, weapon, group, &g_ammoClip);   
            }
            g_step = 5;
            uint32_t sinceShot = (uint32_t)rdr2_call1(N_TIME_SINCE_PED_LAST_SHOT, (uint64_t)(int64_t)ped);
            g_step = 6;
            int      mount     = (int)rdr2_call1(N_GET_MOUNT, (uint64_t)(int64_t)ped);

            float horseSpeed = 0.0f;
            if (mount != 0) {
                
                g_step = 7;
                uint64_t bits = rdr2_call1(N_GET_ENTITY_SPEED, (uint64_t)(int64_t)mount);
                float v;
                memcpy(&v, &bits, sizeof(v));
                if (v == v && v < 1000.0f) horseSpeed = v;   
            }

            
            g_step = 8;
            g_state->seq++;                 
            MemoryBarrier();

            g_state->magic         = GPRDR2_MAGIC;
            g_state->version       = GPRDR2_VERSION;
            g_state->tickMs        = GetTickCount();
            g_state->frame         = ++g_frame;
            g_state->writerPid     = GetCurrentProcessId();

            g_state->weaponHash    = weapon;
            g_state->weaponGroup   = group;
            g_state->ammoInClip    = ammo;
            g_state->timeSinceShot = sinceShot;

            g_state->shooting  = (uint8_t)(rdr2_call1(N_IS_PED_SHOOTING, (uint64_t)(int64_t)ped) != 0);
            

            g_state->aiming    = (uint8_t)(rdr2_call1(N_IS_PLAYER_FREE_AIMING, 0) != 0);
            g_state->reloading = (uint8_t)(rdr2_call1(N_IS_PED_RELOADING, (uint64_t)(int64_t)ped) != 0);
            g_state->onFoot    = (uint8_t)(rdr2_call1(N_IS_PED_ON_FOOT, (uint64_t)(int64_t)ped) != 0);
            g_state->onMount   = (uint8_t)(mount != 0);
            g_state->inVehicle = (uint8_t)(rdr2_call2(N_IS_PED_IN_ANY_VEHICLE, (uint64_t)(int64_t)ped, 0) != 0);
            


            g_state->menuActive = (uint8_t)(rdr2_call1(N_IS_PLAYER_CONTROL_ON, 0) == 0);
            


        g_state->armed      = (uint8_t)(weapon != 0 && weapon != 0xA2719263u);

            
    g_state->mountJumping = 0;
    g_state->mountFalling = 0;
    g_state->mountHeight  = 0.0f;
    if (mount != 0) {
        g_state->mountJumping = (uint8_t)(rdr2_call1(N_IS_PED_JUMPING, (uint64_t)(int64_t)mount) != 0);
        g_state->mountFalling = (uint8_t)(rdr2_call1(N_IS_PED_FALLING, (uint64_t)(int64_t)mount) != 0);
        

        uint64_t bits = rdr2_call1(N_GET_ENTITY_HEIGHT_ABOVE_GROUND, (uint64_t)(int64_t)mount);
        float h; memcpy(&h, &bits, sizeof(h));
        g_state->mountHeight = (h == h && h >= 0.0f && h < 500.0f) ? h : 0.0f;

        
        g_state->mountHurt =
            (uint8_t)(rdr2_call1(N_HAS_ENTITY_BEEN_DAMAGED_BY_ANY_PED, (uint64_t)(int64_t)mount) != 0 ||
                      rdr2_call1(N_HAS_ENTITY_BEEN_DAMAGED_BY_ANY_OBJECT, (uint64_t)(int64_t)mount) != 0 ||
                      rdr2_call1(N_HAS_ENTITY_BEEN_DAMAGED_BY_ANY_VEHICLE, (uint64_t)(int64_t)mount) != 0);
    }

    







    {
        g_state->inTrain       = 0;
        g_state->vehicleModel  = 0;
        g_state->vehicleSpeed  = 0.0f;
        g_state->trainNearby   = 0;
        g_state->trainDist     = 0.0f;
        g_state->trainApproach = 0.0f;

        g_step = 20;
        int veh = (int)rdr2_call2(N_GET_VEHICLE_PED_IS_IN, (uint64_t)(int64_t)ped, 0);
        if (veh != 0) {
            g_state->inVehicle = 1;
            g_step = 21;
            g_state->vehicleModel = (uint32_t)rdr2_call1(N_GET_ENTITY_MODEL, (uint64_t)(int64_t)veh);
            g_step = 22;
            uint64_t sb = rdr2_call1(N_GET_ENTITY_SPEED, (uint64_t)(int64_t)veh);
            float vs; memcpy(&vs, &sb, sizeof(vs));
            g_state->vehicleSpeed = (vs == vs && vs >= 0.0f && vs < 200.0f) ? vs : 0.0f;
        }
        g_step = 23;
        if (rdr2_call1(N_IS_PED_IN_ANY_TRAIN, (uint64_t)(int64_t)ped) != 0 ||
            rdr2_call1(N_IS_PLAYER_RIDING_TRAIN, 0) != 0) {
            g_state->inTrain = 1;
        }

        


        g_step = 24;
        {
            uint64_t ca[3] = { (uint64_t)(int64_t)ped, 1, 0 };   
            uint64_t* v3 = rdr2_callArgsPtr(N_GET_ENTITY_COORDS, ca, 3);
            if (v3) {
                float px, py, pz;
                memcpy(&px, &((float*)v3)[0], 4);
                memcpy(&py, &((float*)v3)[1], 4);
                memcpy(&pz, &((float*)v3)[2], 4);
                if (px == px && py == py && pz == pz) {
                    g_step = 25;
                    uint64_t qa[6];
                    float fx = px, fy = py, fz = pz;
                    memcpy(&qa[0], &fx, 4); memcpy(&qa[1], &fy, 4); memcpy(&qa[2], &fz, 4);
                    float rad = 60.0f;  memcpy(&qa[3], &rad, 4);   
                    qa[4] = 0;                                     
                    qa[5] = 0;
                    int nearVeh = (int)rdr2_callArgs(N_GET_CLOSEST_VEHICLE, qa, 6);   
                    if (nearVeh != 0 && nearVeh != veh) {
                        g_step = 26;
                        uint32_t nm = (uint32_t)rdr2_call1(N_GET_ENTITY_MODEL, (uint64_t)(int64_t)nearVeh);
                        if (rdr2_call1(N_IS_THIS_MODEL_A_TRAIN, (uint64_t)nm) != 0) {
                            
                            uint64_t na[3] = { (uint64_t)(int64_t)nearVeh, 1, 0 };
                            uint64_t* n3 = rdr2_callArgsPtr(N_GET_ENTITY_COORDS, na, 3);
                            if (n3) {
                                float nx, ny, nz;
                                memcpy(&nx, &((float*)n3)[0], 4);
                                memcpy(&ny, &((float*)n3)[1], 4);
                                memcpy(&nz, &((float*)n3)[2], 4);
                                float dx = nx - px, dy = ny - py, dz = nz - pz;
                                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                                if (d == d && d < 500.0f) {
                                    g_state->trainNearby = 1;
                                    g_state->trainDist   = d;
                                    
                                    static float sPrevDist = 0.0f;
                                    static DWORD sPrevTick = 0;
                                    DWORD wt = GetTickCount();
                                    if (sPrevTick != 0 && wt > sPrevTick && sPrevDist > 0.0f) {
                                        float dt = (float)(wt - sPrevTick) / 1000.0f;
                                        if (dt > 0.02f && dt < 1.0f) {
                                            float app = (sPrevDist - d) / dt;
                                            
                                            g_state->trainApproach =
                                                g_state->trainApproach * 0.5f + app * 0.5f;
                                        }
                                    }
                                    sPrevDist = d; sPrevTick = wt;
                                }
                            }
                        }
                    }
                    g_step = 27;
                }
            }
        }
    }

    

    {
        static float sPrevSpeed = 0.0f;
        static DWORD sPrevTick  = 0;
        static float sAccel     = 0.0f;
        DWORD wt = GetTickCount();
        if (sPrevTick == 0 || wt <= sPrevTick) {
            sPrevSpeed = horseSpeed; sPrevTick = wt;
        } else if (wt - sPrevTick >= 100) {
            float a = (horseSpeed - sPrevSpeed) * 1000.0f / (float)(wt - sPrevTick);
            sAccel = sAccel * 0.5f + a * 0.5f;         
            sPrevSpeed = horseSpeed; sPrevTick = wt;
        }
        g_state->horseAccel = sAccel;
    }

    

    g_state->horseGait = 255;
    if (mount != 0) {
        if (rdr2_call1(N_IS_PED_SPRINTING, (uint64_t)(int64_t)mount) != 0)
            g_state->horseGait = 2;
        else if (rdr2_call1(N_IS_PED_RUNNING, (uint64_t)(int64_t)mount) != 0)
            g_state->horseGait = 1;
        else
            g_state->horseGait = 0;
    }

    
    g_state->jumping  = (uint8_t)(rdr2_call1(N_IS_PED_JUMPING,  (uint64_t)(int64_t)ped) != 0);
    g_state->falling  = (uint8_t)(rdr2_call1(N_IS_PED_FALLING,  (uint64_t)(int64_t)ped) != 0);
    g_state->climbing = (uint8_t)(rdr2_call1(N_IS_PED_CLIMBING, (uint64_t)(int64_t)ped) != 0);
    g_state->vaulting = (uint8_t)(rdr2_call1(N_IS_PED_VAULTING, (uint64_t)(int64_t)ped) != 0);
    g_state->swimming = (uint8_t)(rdr2_call1(N_IS_PED_SWIMMING, (uint64_t)(int64_t)ped) != 0);
    g_state->inCover  = (uint8_t)(rdr2_call3(N_IS_PED_IN_COVER, (uint64_t)(int64_t)ped, 0, 0) != 0);

    
    if (rdr2_call1(N_IS_PED_SPRINTING, (uint64_t)(int64_t)ped) != 0)
        g_state->playerGait = 2;
    else if (rdr2_call1(N_IS_PED_RUNNING, (uint64_t)(int64_t)ped) != 0)
        g_state->playerGait = 1;
    else
        g_state->playerGait = 0;

    g_state->deadOrDying = (uint8_t)(rdr2_call2(N_IS_PED_DEAD_OR_DYING, (uint64_t)(int64_t)ped, 0) != 0);
    g_state->prone       = (uint8_t)(rdr2_call1(N_IS_PED_PRONE,       (uint64_t)(int64_t)ped) != 0);
    g_state->grounded    = (uint8_t)(!g_state->jumping && !g_state->falling &&
                                     !g_state->climbing && !g_state->vaulting &&
                                     !g_state->swimming && !g_state->onMount);

    
    g_state->slowMotion = (uint8_t)(g_state->timeScale < 0.95f && g_state->timeScale > 0.05f);
    g_state->uiOverlay  = (uint8_t)(!g_state->menuActive &&
                                    rdr2_call1(N_IS_PLAYER_CONTROL_ON, 0) == 0);

    g_state->health    = (int)rdr2_call1(N_GET_ENTITY_HEALTH, (uint64_t)(int64_t)ped);
    g_state->maxHealth = (int)rdr2_call1(N_GET_PED_MAX_HEALTH, (uint64_t)(int64_t)ped);

    
    {
        uint64_t bits = rdr2_call1(N_GET_ENTITY_SPEED, (uint64_t)(int64_t)ped);
        float v; memcpy(&v, &bits, sizeof(v));
        g_state->playerSpeed = (v == v && v < 1000.0f) ? v : 0.0f;
    }

    




    {
        static DWORD sLastWall = 0;
        static int   sLastGame = 0;
        static float sScale    = 1.0f;
        int  gt = (int)rdr2_call0(N_GET_GAME_TIMER);
        DWORD wt = GetTickCount();
        if (sLastWall == 0) {
            sLastWall = wt; sLastGame = gt;
        } else if ((DWORD)(wt - sLastWall) >= 250) {
            int  dg = gt - sLastGame;
            DWORD dw = wt - sLastWall;
            if (dg >= 0 && dw > 0) {
                float r = (float)dg / (float)dw;
                if (r > 4.0f) r = 4.0f;
                sScale = sScale * 0.5f + r * 0.5f;   
            }
            sLastWall = wt; sLastGame = gt;
        }
        g_state->timeScale = sScale;
    }

    g_state->horseSpeed  = horseSpeed;
            g_state->playerSpeed = 0.0f;
            g_state->mountHash   = 0;
            g_state->playerPed   = (uint32_t)ped;

            MemoryBarrier();
            g_state->seq++;                 

            ++g_bootFrames;
            if (g_bootFrames == 120) {
                
                Log("状态样本：武器=0x%08X 组=0x%08X(%s) 弹匣=%d 瞄准=%u 持械=%u 菜单=%u 骑马=%u",
                    weapon, group, GroupName(group), ammo, g_state->aiming, g_state->armed,
                    g_state->menuActive, g_state->onMount);
            }

            g_step = 9;
            




            {
                static uint32_t lastWeapon = 0xFFFFFFFFu, lastGroup = 0xFFFFFFFFu;
                static uint8_t  lastFlags  = 0xFF;

                uint8_t flags = (uint8_t)((g_state->aiming    ? 1 : 0) |
                                          (g_state->armed     ? 2 : 0) |
                                          (g_state->onMount   ? 4 : 0) |
                                          (g_state->menuActive? 8 : 0) |
                                          (g_state->reloading ? 16 : 0) |
                                          (g_state->onFoot    ? 32 : 0) |
                                          (g_state->shooting  ? 64 : 0));

                if (weapon != lastWeapon) {
                    lastWeapon = weapon;
                    Log("换武器：0x%08X（组 %s）", weapon, GroupName(group));
                }
                if (group != lastGroup) {
                    lastGroup = group;
                    Log("换武器组：0x%08X（%s）", group, GroupName(group));
                }
                

                {
                    static uint32_t lastVeh = 0xFFFFFFFFu;
                    static uint8_t  lastTrain = 0xFF;
                    if (g_state->vehicleModel != lastVeh) {
                        lastVeh = g_state->vehicleModel;
                        Log("载具：模型=0x%08X 火车=%s 速度=%.1f",
                            g_state->vehicleModel, g_state->inTrain ? "是" : "否",
                            (double)g_state->vehicleSpeed);
                    }
                    uint8_t tr = (uint8_t)((g_state->inTrain ? 1 : 0) |
                                           (g_state->trainNearby ? 2 : 0));
                    if (tr != lastTrain) {
                        lastTrain = tr;
                        Log("火车状态：在车上=%s 附近有火车=%s（距离 %.1f 米）",
                            (tr & 1) ? "是" : "否", (tr & 2) ? "是" : "否",
                            (double)g_state->trainDist);
                    }
                }

                
        {
            static uint8_t lastGait = 255;
            if (g_state->horseGait != lastGait) {
                lastGait = g_state->horseGait;
                const char* names[] = {"走步(4拍)", "小跑/坎特", "疾驰(4拍)", "不在马上"};
                Log("坐骑步态 -> %s（马速 %.1f）",
                    g_state->horseGait <= 2 ? names[g_state->horseGait] : names[3],
                    (double)g_state->horseSpeed);
            }
        }

        if (flags != lastFlags) {
                    lastFlags = flags;
                    Log("动作：开枪=%s 瞄准=%s 持械=%s 骑马=%s 菜单=%s 装弹=%s 步行=%s",
                        g_state->shooting ? "是" : "否",
                        g_state->aiming ? "是" : "否",
                        g_state->armed ? "是" : "否",
                        g_state->onMount ? "是" : "否",
                        g_state->menuActive ? "是" : "否",
                        g_state->reloading ? "是" : "否",
                        g_state->onFoot ? "是" : "否");
                }
            }
            

            if ((++g_frames % 300) == 0) {
                uint32_t f = (uint32_t)(g_frames / 300);
                Log("存活：已跑 %u00 帧（ped=%d 武器=0x%08X 组=0x%08X 弹匣=%d）",
                    f, ped, weapon, group, ammo);

                


                int pauseMenu = (int)rdr2_call0(N_IS_PAUSE_MENU_ACTIVE);
                int hudHidden = (int)rdr2_call0(N_IS_HUD_HIDDEN);
                int controlOn = (int)rdr2_call1(N_IS_PLAYER_CONTROL_ON, 0);
                Log("坐骑诊断：跳=%u 坠=%u 高度=%.2fm 加速度=%.1f/s2 受伤=%u "
                    "（跳一次后看这行，就知道落地信号到底长什么样）",
                    g_state->mountJumping, g_state->mountFalling,
                    (double)g_state->mountHeight, (double)g_state->horseAccel,
                    g_state->mountHurt);
                Log("载具诊断：在车=%u 火车=%u 模型=0x%08X 车速=%.1f 附近火车=%u 距离=%.1f 接近=%.1f",
                    g_state->inVehicle, g_state->inTrain, g_state->vehicleModel,
                    (double)g_state->vehicleSpeed, g_state->trainNearby,
                    (double)g_state->trainDist, (double)g_state->trainApproach);
                Log("诊断：持械=%u 瞄准=%u 开枪=%u 装弹=%u 骑马=%u 菜单=%u "
                    "弹匣(总量)=%d 弹匣(弹夹)=%d ｜ 菜单候选: 暂停菜单native=%d HUD隐藏=%d 有控制权=%d",
                    g_state->armed, g_state->aiming, g_state->shooting, g_state->reloading,
                    g_state->onMount, g_state->menuActive, ammo, g_ammoClip,
                    pauseMenu, hudHidden, controlOn);
            }

        } __except (g_excCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
            if (!g_faulted) {
                g_faulted = TRUE;
                Log("脚本内部异常：第 %ld 步 异常码 0x%08lX —— 已跳过该帧继续运行；"
                    "请把这一行发给开发者", g_step, (unsigned long)g_excCode);
            }
        }

        g_step = 0;
        sh::scriptWait(0);          
    }
}





DWORD WINAPI BootThread(LPVOID) {
    


    for (int i = 0; i < 3000; ++i) {         
        if (sh::Resolve()) {
            if (InterlockedCompareExchange(&g_registered, 1, 0) != 0) return 0;
            OpenState();
            sh::scriptRegister(g_self, Tick);
            Log("已向 ScriptHookRDR2 注册脚本（等了 %d ms）", i * 100);
            return 0;
        }
        if (i > 0 && (i % 100) == 0)
            Log("等待 ScriptHookRDR2... 已等 %d 秒（%s）", i / 10, sh::Report());
        Sleep(100);
    }
    Log("等待 ScriptHookRDR2 超时（5 分钟）—— 脚本未注册，代理会退回只看扳机（%s）",
        sh::Report());
    return 0;
}

}  



extern "C" __declspec(dllexport) void ScriptMain(void) {
    if (!sh::Resolve()) {
        Log("ScriptMain 被调用，但解析 ScriptHookRDR2 入口失败");
        return;
    }
    if (InterlockedCompareExchange(&g_registered, 1, 0) != 0) return;
    OpenState();
    sh::scriptRegister(g_self, Tick);
    Log("ScriptMain 路径注册成功");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        
        HANDLE t = CreateThread(nullptr, 0, BootThread, nullptr, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
