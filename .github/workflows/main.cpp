#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

bool g_MenuOpen = false;
int  g_Cursor   = 0;

struct WeaponProfile {
    const char* name;
    uint32_t    rapidStep;
    int         defaultRecoilY;
    int         recoilY;
};

WeaponProfile g_Profiles[] = {
    { "Rajada (Burst)",  2, 14, 14 },
    { "Rifle Variavel",  2, 10, 10 },
    { "Semi-Auto",       3,  8,  8 },
    { "9mm / Revolver",  2,  6,  6 },
    { "Anti-Recoil 0",   2,  0,  0 }
};

constexpr int NUM_PROFILES = 5;
int g_CurrentProfile = 0;

struct PadMods {
    bool useL1R1    = false; // false = L2+R2 | true = L1+R1
    bool rapidFire  = true;
    bool stickyAim  = true;
    bool autoMarkR3 = true;
} g_Mods;

constexpr int MENU_ITEMS = 6;

uint32_t g_FrameCounter = 0;
uint32_t g_PrevButtons  = 0;

constexpr int      STICKY_AIM_STRENGTH = 4;
constexpr uint32_t AUTO_MARK_INTERVAL  = 25;
constexpr uint32_t AUTO_MARK_HOLD      = 3;

typedef int (*scePadRead_t)(int32_t, OrbisPadData*, int32_t);
typedef int (*scePadReadState_t)(int32_t, OrbisPadData*);

scePadRead_t      g_OrigScePadRead      = nullptr;
scePadReadState_t g_OrigScePadReadState = nullptr;
uint64_t*         g_GotSlotRead         = nullptr;
uint64_t*         g_GotSlotReadState    = nullptr;
volatile bool     g_Running             = true;

void PadNotify(const char* fmt, ...) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.targetId = -1;
    req.userId = -1;
    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

uint8_t ClampStick(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

void ShowCurrentOptionStatus() {
    WeaponProfile& wp = g_Profiles[g_CurrentProfile];
    switch (g_Cursor) {
        case 0: PadNotify("Arma: %s (Recuo: %d)", wp.name, wp.recoilY); break;
        case 1: PadNotify("Anti-Recoil Y: %d [Esq/Dir | X=Zerar]", wp.recoilY); break;
        case 2: PadNotify("Rapid Fire: %s", g_Mods.rapidFire ? "ON" : "OFF"); break;
        case 3: PadNotify("Assistencia Mira: %s", g_Mods.stickyAim ? "ON" : "OFF"); break;
        case 4: PadNotify("Auto Marcar R3: %s", g_Mods.autoMarkR3 ? "ON" : "OFF"); break;
        case 5: PadNotify("Layout: %s", g_Mods.useL1R1 ? "L1 + R1" : "L2 + R2"); break;
    }
}

void AdjustCurrentOption(int dir) {
    WeaponProfile& wp = g_Profiles[g_CurrentProfile];
    switch (g_Cursor) {
        case 0:
            g_CurrentProfile = (g_CurrentProfile + dir + NUM_PROFILES) % NUM_PROFILES;
            PadNotify("Arma: %s (Recuo: %d)", g_Profiles[g_CurrentProfile].name, g_Profiles[g_CurrentProfile].recoilY);
            break;
        case 1:
            if (dir == 0) {
                wp.recoilY = 0;
                PadNotify("Anti-Recoil ZERADO (0) [%s]", wp.name);
            } else {
                wp.recoilY += dir;
                if (wp.recoilY < 0) wp.recoilY = 0;
                if (wp.recoilY > 40) wp.recoilY = 40;
                PadNotify("Anti-Recoil Y: %d [%s]", wp.recoilY, wp.name);
            }
            break;
        case 2:
            g_Mods.rapidFire = !g_Mods.rapidFire;
            PadNotify("Rapid Fire: %s", g_Mods.rapidFire ? "ON" : "OFF");
            break;
        case 3:
            g_Mods.stickyAim = !g_Mods.stickyAim;
            PadNotify("Assistencia Mira: %s", g_Mods.stickyAim ? "ON" : "OFF");
            break;
        case 4:
            g_Mods.autoMarkR3 = !g_Mods.autoMarkR3;
            PadNotify("Auto Marcar R3: %s", g_Mods.autoMarkR3 ? "ON" : "OFF");
            break;
        case 5:
            g_Mods.useL1R1 = !g_Mods.useL1R1;
            PadNotify("Layout: %s", g_Mods.useL1R1 ? "L1 + R1" : "L2 + R2");
            break;
    }
}

void ProcessPadPackets(OrbisPadData* pData, int count) {
    g_FrameCounter++;
    OrbisPadData& latest = pData[count - 1];
    uint32_t btn = latest.buttons;
    uint32_t newlyPressed = btn & ~g_PrevButtons;

    bool toggleCombo = ((btn & ORBIS_PAD_BUTTON_SQUARE) && (newlyPressed & ORBIS_PAD_BUTTON_RIGHT)) ||
                       ((btn & ORBIS_PAD_BUTTON_RIGHT) && (newlyPressed & ORBIS_PAD_BUTTON_SQUARE));

    if (toggleCombo) {
        g_MenuOpen = !g_MenuOpen;
        if (g_MenuOpen) {
            PadNotify("Menu Aberto [Cima/Baixo | Esq/Dir | X]");
            ShowCurrentOptionStatus();
        } else {
            PadNotify("Menu Fechado");
        }
    }

    if (!g_MenuOpen && !toggleCombo && (btn & ORBIS_PAD_BUTTON_SQUARE)) {
        if (newlyPressed & ORBIS_PAD_BUTTON_LEFT) {
            g_CurrentProfile = (g_CurrentProfile + 1) % NUM_PROFILES;
            PadNotify("Arma: %s | Recuo: %d", g_Profiles[g_CurrentProfile].name, g_Profiles[g_CurrentProfile].recoilY);
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_DOWN) {
            WeaponProfile& wp = g_Profiles[g_CurrentProfile];
            if (wp.recoilY != 0) {
                wp.recoilY = 0;
                PadNotify("Anti-Recoil ZERADO (0) [%s]", wp.name);
            } else {
                wp.recoilY = wp.defaultRecoilY;
                PadNotify("Anti-Recoil Restaurado: %d [%s]", wp.recoilY, wp.name);
            }
        }
    }

    if (g_MenuOpen && !toggleCombo) {
        if (newlyPressed & ORBIS_PAD_BUTTON_UP) {
            g_Cursor = (g_Cursor - 1 + MENU_ITEMS) % MENU_ITEMS;
            ShowCurrentOptionStatus();
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_DOWN) {
            g_Cursor = (g_Cursor + 1) % MENU_ITEMS;
            ShowCurrentOptionStatus();
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_LEFT) {
            AdjustCurrentOption(-1);
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_RIGHT) {
            AdjustCurrentOption(1);
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_CROSS) {
            AdjustCurrentOption(g_Cursor == 1 ? 0 : 1);
        }
    }

    g_PrevButtons = btn;

    WeaponProfile& activeWp = g_Profiles[g_CurrentProfile];
    uint32_t aimBtn  = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_L1 : ORBIS_PAD_BUTTON_L2;
    uint32_t fireBtn = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_R1 : ORBIS_PAD_BUTTON_R2;

    bool isAiming = (btn & aimBtn) != 0;
    bool isFiring = (btn & fireBtn) != 0;

    uint32_t step = activeWp.rapidStep > 0 ? activeWp.rapidStep : 2;
    bool releasePulse = ((g_FrameCounter / step) % 2) == 1;

    int stickOffsetX = 0;
    int stickOffsetY = 0;

    if (g_Mods.stickyAim && isAiming) {
        int phase = g_FrameCounter % 4;
        if (isFiring) {
            stickOffsetX = (phase % 2 == 0) ? STICKY_AIM_STRENGTH : -STICKY_AIM_STRENGTH;
        } else {
            if (phase == 0)      { stickOffsetX =  STICKY_AIM_STRENGTH; }
            else if (phase == 1) { stickOffsetY =  STICKY_AIM_STRENGTH; }
            else if (phase == 2) { stickOffsetX = -STICKY_AIM_STRENGTH; }
            else                 { stickOffsetY = -STICKY_AIM_STRENGTH; }
        }
    }

    if (isAiming && isFiring && activeWp.recoilY > 0) {
        stickOffsetY += activeWp.recoilY;
    }

    bool doMark = g_Mods.autoMarkR3 && isAiming && ((g_FrameCounter % AUTO_MARK_INTERVAL) < AUTO_MARK_HOLD);

    for (int i = 0; i < count; i++) {
        OrbisPadData& pkt = pData[i];

        if (toggleCombo) {
            pkt.buttons &= ~ORBIS_PAD_BUTTON_RIGHT;
        }
        if (!g_MenuOpen && (btn & ORBIS_PAD_BUTTON_SQUARE)) {
            pkt.buttons &= ~(ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_DOWN);
        }
        if (g_MenuOpen) {
            pkt.buttons &= ~(ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_RIGHT | ORBIS_PAD_BUTTON_CROSS);
        }

        if (g_Mods.rapidFire && isFiring) {
            if (releasePulse) {
                pkt.buttons &= ~fireBtn;
                if (!g_Mods.useL1R1) pkt.analogButtons.r2 = 0;
            } else {
                pkt.buttons |= fireBtn;
                if (!g_Mods.useL1R1) pkt.analogButtons.r2 = 255;
            }
        }

        if (stickOffsetX != 0 || stickOffsetY != 0) {
            pkt.rightStick.x = ClampStick(static_cast<int>(pkt.rightStick.x) + stickOffsetX);
            pkt.rightStick.y = ClampStick(static_cast<int>(pkt.rightStick.y) + stickOffsetY);
        }

        if (doMark) {
            pkt.buttons |= ORBIS_PAD_BUTTON_R3;
        }
    }
}

int scePadRead_Hook(int32_t handle, OrbisPadData* pData, int32_t num) {
    if (!g_OrigScePadRead) return -1;
    int ret = g_OrigScePadRead(handle, pData, num);
    if (ret > 0 && pData != nullptr) {
        ProcessPadPackets(pData, ret);
    }
    return ret;
}

int scePadReadState_Hook(int32_t handle, OrbisPadData* pData) {
    if (!g_OrigScePadReadState) return -1;
    int ret = g_OrigScePadReadState(handle, pData);
    if (ret == 0 && pData != nullptr) {
        ProcessPadPackets(pData, 1);
    }
    return ret;
}

void* InstallGotHookThread(void*) {
    sceKernelSleep(5);
    PadNotify("TLOU PRX [1/3]: Thread OK");
    sceKernelSleep(1);

    OrbisKernelModule handles[64];
    memset(handles, 0, sizeof(handles));
    size_t numHandles = 0;
    if (sceKernelGetModuleList(handles, 64, &numHandles) < 0 || numHandles == 0) {
        PadNotify("TLOU PRX: Erro ModuleList");
        return nullptr;
    }

    OrbisKernelModule ebootHandle = handles[0];
    OrbisKernelModule padHandle   = 0;

    for (size_t m = 0; m < numHandles; m++) {
        OrbisKernelModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[m], &info) >= 0) {
            if (strstr(info.name, "eboot") != nullptr) {
                ebootHandle = handles[m];
            }
            if (strstr(info.name, "ScePad") != nullptr) {
                padHandle = handles[m];
            }
        }
    }

    if (padHandle == 0) {
        PadNotify("TLOU PRX: libScePad nao carregada");
        return nullptr;
    }

    void* fnRead = nullptr;
    void* fnReadState = nullptr;
    sceKernelDlsym(padHandle, "scePadRead", &fnRead);
    sceKernelDlsym(padHandle, "scePadReadState", &fnReadState);

    if (fnRead == nullptr && fnReadState == nullptr) {
        PadNotify("TLOU PRX: Erro dlsym Pad");
        return nullptr;
    }

    PadNotify("TLOU PRX [2/3]: Buscando GOT...");
    sceKernelSleep(1);

    OrbisKernelModuleInfo modInfo;
    memset(&modInfo, 0, sizeof(modInfo));
    modInfo.size = sizeof(modInfo);
    if (sceKernelGetModuleInfo(ebootHandle, &modInfo) < 0) {
        PadNotify("TLOU PRX: Erro ModuleInfo");
        return nullptr;
    }

    uint64_t targetRead      = reinterpret_cast<uint64_t>(fnRead);
    uint64_t targetReadState = reinterpret_cast<uint64_t>(fnReadState);
    uint64_t hookRead        = reinterpret_cast<uint64_t>(scePadRead_Hook);
    uint64_t hookReadState   = reinterpret_cast<uint64_t>(scePadReadState_Hook);

    for (uint32_t s = 0; s < modInfo.segmentCount && s < 4; s++) {
        int prot = modInfo.segmentInfo[s].prot;
        // Apenas segmentos ja gravaveis (PROT_READ | PROT_WRITE = 0x3) e nao executaveis
        if (prot != 0x3) continue;

        uintptr_t segAddr = reinterpret_cast<uintptr_t>(modInfo.segmentInfo[s].address);
        size_t segSize = modInfo.segmentInfo[s].size;
        if (segAddr < 0x400000 || segSize < 8 || segSize > 0x4000000) continue;

        // Limita a busca aos primeiros 2 MB do segmento de dados (onde fica .got.plt)
        size_t scanBytes = segSize > 0x200000 ? 0x200000 : segSize;
        uint64_t* start = reinterpret_cast<uint64_t*>(segAddr);
        size_t qwords = scanBytes / sizeof(uint64_t);

        for (size_t i = 0; i < qwords; i++) {
            uint64_t val = start[i];
            if (targetRead != 0 && val == targetRead && g_GotSlotRead == nullptr) {
                g_OrigScePadRead = reinterpret_cast<scePadRead_t>(fnRead);
                if (__sync_bool_compare_and_swap(&start[i], targetRead, hookRead)) {
                    g_GotSlotRead = &start[i];
                }
            } else if (targetReadState != 0 && val == targetReadState && g_GotSlotReadState == nullptr) {
                g_OrigScePadReadState = reinterpret_cast<scePadReadState_t>(fnReadState);
                if (__sync_bool_compare_and_swap(&start[i], targetReadState, hookReadState)) {
                    g_GotSlotReadState = &start[i];
                }
            }
        }
    }

    if (g_GotSlotRead != nullptr || g_GotSlotReadState != nullptr) {
        PadNotify("TLOU PRX [3/3]: Ativo! [Quad+Dir Menu]");
    } else {
        PadNotify("TLOU PRX: GOT protegido (RELRO)");
    }
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) int module_start(size_t argc, const void* argv) {
    OrbisPthread tid;
    if (scePthreadCreate(&tid, nullptr, InstallGotHookThread, nullptr, "tlou_hook") == 0) {
        scePthreadDetach(tid);
    }
    return 0;
}

extern "C" __attribute__((visibility("default"))) int module_stop(size_t argc, const void* argv) {
    g_Running = false;
    if (g_GotSlotRead != nullptr && g_OrigScePadRead != nullptr) {
        __sync_bool_compare_and_swap(g_GotSlotRead, reinterpret_cast<uint64_t>(scePadRead_Hook), reinterpret_cast<uint64_t>(g_OrigScePadRead));
    }
    if (g_GotSlotReadState != nullptr && g_OrigScePadReadState != nullptr) {
        __sync_bool_compare_and_swap(g_GotSlotReadState, reinterpret_cast<uint64_t>(scePadReadState_Hook), reinterpret_cast<uint64_t>(g_OrigScePadReadState));
    }
    return 0;
}
