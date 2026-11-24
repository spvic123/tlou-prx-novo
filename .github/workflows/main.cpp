#include <Common.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

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

Detour Detour_scePadReadState;
Detour Detour_scePadRead;

void PadNotify(const char* fmt, ...) {
    OrbisNotificationRequest req = {};
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

void ProcessPadData(OrbisPadData* pData) {
    g_FrameCounter++;
    uint32_t btn = pData->buttons;
    uint32_t newlyPressed = btn & ~g_PrevButtons;

    // Quadrado + Direita (em qualquer ordem): Abre ou fecha o menu
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
        pData->buttons &= ~ORBIS_PAD_BUTTON_RIGHT;
    }

    // Atalhos rapidos fora do menu (segurando Quadrado)
    if (!g_MenuOpen && !toggleCombo && (btn & ORBIS_PAD_BUTTON_SQUARE)) {
        if (newlyPressed & ORBIS_PAD_BUTTON_LEFT) {
            g_CurrentProfile = (g_CurrentProfile + 1) % NUM_PROFILES;
            PadNotify("Arma: %s | Recuo: %d", g_Profiles[g_CurrentProfile].name, g_Profiles[g_CurrentProfile].recoilY);
            pData->buttons &= ~ORBIS_PAD_BUTTON_LEFT;
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
            pData->buttons &= ~ORBIS_PAD_BUTTON_DOWN;
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
        pData->buttons &= ~(ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_RIGHT | ORBIS_PAD_BUTTON_CROSS);
    }

    g_PrevButtons = btn;

    WeaponProfile& activeWp = g_Profiles[g_CurrentProfile];
    uint32_t aimBtn  = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_L1 : ORBIS_PAD_BUTTON_L2;
    uint32_t fireBtn = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_R1 : ORBIS_PAD_BUTTON_R2;

    bool isAiming = (btn & aimBtn) != 0;
    bool isFiring = (btn & fireBtn) != 0;

    if (g_Mods.rapidFire && isFiring) {
        uint32_t step = activeWp.rapidStep > 0 ? activeWp.rapidStep : 2;
        bool releasePulse = ((g_FrameCounter / step) % 2) == 1;
        if (releasePulse) {
            pData->buttons &= ~fireBtn;
            if (!g_Mods.useL1R1) pData->analogButtons.r2 = 0;
        } else {
            pData->buttons |= fireBtn;
            if (!g_Mods.useL1R1) pData->analogButtons.r2 = 255;
        }
    }

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

    if (stickOffsetX != 0 || stickOffsetY != 0) {
        pData->rightStick.x = ClampStick(static_cast<int>(pData->rightStick.x) + stickOffsetX);
        pData->rightStick.y = ClampStick(static_cast<int>(pData->rightStick.y) + stickOffsetY);
    }

    if (g_Mods.autoMarkR3 && isAiming) {
        if ((g_FrameCounter % AUTO_MARK_INTERVAL) < AUTO_MARK_HOLD) {
            pData->buttons |= ORBIS_PAD_BUTTON_R3;
        }
    }
}

int scePadReadState_Hook(int32_t handle, OrbisPadData* pData) {
    typedef int (*scePadReadState_t)(int32_t, OrbisPadData*);
    scePadReadState_t origFn = reinterpret_cast<scePadReadState_t>(Detour_scePadReadState.StubPtr);
    if (!origFn) return -1;
    int ret = origFn(handle, pData);
    if (ret == 0 && pData != nullptr) {
        ProcessPadData(pData);
    }
    return ret;
}

int scePadRead_Hook(int32_t handle, OrbisPadData* pData, int32_t num) {
    typedef int (*scePadRead_t)(int32_t, OrbisPadData*, int32_t);
    scePadRead_t origFn = reinterpret_cast<scePadRead_t>(Detour_scePadRead.StubPtr);
    if (!origFn) return -1;
    int ret = origFn(handle, pData, num);
    if (ret > 0 && pData != nullptr) {
        ProcessPadData(&pData[0]);
    }
    return ret;
}

extern "C" __attribute__((visibility("default"))) int module_start(size_t argc, const void* argv) {
    int hooksCount = 0;
    int padHandle = sceKernelLoadStartModule("libScePad.sprx", 0, nullptr, 0, nullptr, nullptr);
    if (padHandle >= 0) {
        void* fnReadState = nullptr;
        if (sceKernelDlsym(padHandle, "scePadReadState", &fnReadState) >= 0 && fnReadState != nullptr) {
            Detour_Construct(&Detour_scePadReadState, static_cast<DetourMode>(1));
            Detour_DetourFunction(&Detour_scePadReadState, reinterpret_cast<uint64_t>(fnReadState), reinterpret_cast<void*>(scePadReadState_Hook));
            hooksCount++;
        }
        void* fnRead = nullptr;
        if (sceKernelDlsym(padHandle, "scePadRead", &fnRead) >= 0 && fnRead != nullptr) {
            Detour_Construct(&Detour_scePadRead, static_cast<DetourMode>(1));
            Detour_DetourFunction(&Detour_scePadRead, reinterpret_cast<uint64_t>(fnRead), reinterpret_cast<void*>(scePadRead_Hook));
            hooksCount++;
        }
    }
    PadNotify("TLOU PRX v2 Carregado! [Hooks: %d]", hooksCount);
    return 0;
}

extern "C" __attribute__((visibility("default"))) int module_stop(size_t argc, const void* argv) {
    if (Detour_scePadReadState.StubPtr != nullptr) {
        Detour_RestoreFunction(&Detour_scePadReadState);
        Detour_Destroy(&Detour_scePadReadState);
    }
    if (Detour_scePadRead.StubPtr != nullptr) {
        Detour_RestoreFunction(&Detour_scePadRead);
        Detour_Destroy(&Detour_scePadRead);
    }
    return 0;
}
