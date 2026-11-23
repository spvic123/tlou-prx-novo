#include <Common.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <cstdio>
#include <cstdint>
#include <cstdarg>

bool g_MenuOpen = false;
int  g_Cursor   = 0;

struct PadMods {
    bool useL1R1     = false; // false = L2+R2 | true = L1+R1
    bool rapidFire   = true;  // Rapid Fire ligado por padrão
    bool stickyAim   = true;  // Assistência de mira ligada por padrão
    bool autoMarkR3  = true;  // Auto marcação R3 ao mirar ligada por padrão
} g_Mods;

constexpr int MENU_ITEMS = 4;

uint32_t g_FrameCounter = 0;
uint32_t g_PrevButtons  = 0;

constexpr uint32_t RAPID_FIRE_STEP    = 2;
constexpr int      STICKY_AIM_STRENGTH = 4;
constexpr uint32_t AUTO_MARK_INTERVAL = 25;
constexpr uint32_t AUTO_MARK_HOLD     = 3;

Detour Detour_scePadReadState;

void Notify(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    NotifyStatic(buf);
}

uint8_t ClampStick(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

void ShowCurrentOptionStatus() {
    switch (g_Cursor) {
        case 0: Notify("Layout: %s", g_Mods.useL1R1 ? "L1 + R1" : "L2 + R2"); break;
        case 1: Notify("Rapid Fire: %s", g_Mods.rapidFire ? "ON" : "OFF"); break;
        case 2: Notify("Assistencia Mira: %s", g_Mods.stickyAim ? "ON" : "OFF"); break;
        case 3: Notify("Auto ADS + Marcar R3: %s", g_Mods.autoMarkR3 ? "ON" : "OFF"); break;
    }
}

void ToggleCurrentOption() {
    switch (g_Cursor) {
        case 0:
            g_Mods.useL1R1 = !g_Mods.useL1R1;
            Notify("Layout Alterado: %s", g_Mods.useL1R1 ? "L1 + R1" : "L2 + R2");
            break;
        case 1:
            g_Mods.rapidFire = !g_Mods.rapidFire;
            Notify("Rapid Fire: %s", g_Mods.rapidFire ? "ON" : "OFF");
            break;
        case 2:
            g_Mods.stickyAim = !g_Mods.stickyAim;
            Notify("Assistencia Mira: %s", g_Mods.stickyAim ? "ON" : "OFF");
            break;
        case 3:
            g_Mods.autoMarkR3 = !g_Mods.autoMarkR3;
            Notify("Auto ADS + Marcar R3: %s", g_Mods.autoMarkR3 ? "ON" : "OFF");
            break;
    }
}

int scePadReadState_Hook(int32_t handle, OrbisPadData* pData) {
    int ret = Detour_scePadReadState.Invoke<int>(handle, pData);
    if (ret != 0 || pData == nullptr) {
        return ret;
    }

    g_FrameCounter++;
    uint32_t btn = pData->buttons;
    uint32_t newlyPressed = btn & ~g_PrevButtons;

    if ((btn & ORBIS_PAD_BUTTON_SQUARE) && (newlyPressed & ORBIS_PAD_BUTTON_RIGHT)) {
        g_MenuOpen = !g_MenuOpen;
        if (g_MenuOpen) {
            Notify("Menu Aberto [Cima/Baixo | X]");
            ShowCurrentOptionStatus();
        } else {
            Notify("Menu Fechado");
        }
    }

    if (g_MenuOpen) {
        if (newlyPressed & ORBIS_PAD_BUTTON_UP) {
            g_Cursor = (g_Cursor - 1 + MENU_ITEMS) % MENU_ITEMS;
            ShowCurrentOptionStatus();
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_DOWN) {
            g_Cursor = (g_Cursor + 1) % MENU_ITEMS;
            ShowCurrentOptionStatus();
        }
        if (newlyPressed & ORBIS_PAD_BUTTON_CROSS) {
            ToggleCurrentOption();
        }
        pData->buttons &= ~(ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_CROSS);
    }

    g_PrevButtons = btn;

    uint32_t aimBtn  = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_L1 : ORBIS_PAD_BUTTON_L2;
    uint32_t fireBtn = g_Mods.useL1R1 ? ORBIS_PAD_BUTTON_R1 : ORBIS_PAD_BUTTON_R2;

    bool isAiming = (btn & aimBtn) != 0;
    bool isFiring = (btn & fireBtn) != 0;

    if (g_Mods.rapidFire && isFiring) {
        bool releasePulse = ((g_FrameCounter / RAPID_FIRE_STEP) % 2) == 1;
        if (releasePulse) {
            pData->buttons &= ~fireBtn;
            if (!g_Mods.useL1R1) pData->analogButtons.r2 = 0;
        } else {
            pData->buttons |= fireBtn;
            if (!g_Mods.useL1R1) pData->analogButtons.r2 = 255;
        }
    }

    if (g_Mods.stickyAim && isAiming) {
        int phase = g_FrameCounter % 4;
        int offsetX = 0;
        int offsetY = 0;

        if (phase == 0)      { offsetX =  STICKY_AIM_STRENGTH; }
        else if (phase == 1) { offsetY =  STICKY_AIM_STRENGTH; }
        else if (phase == 2) { offsetX = -STICKY_AIM_STRENGTH; }
        else                 { offsetY = -STICKY_AIM_STRENGTH; }

        pData->rightStick.x = ClampStick(static_cast<int>(pData->rightStick.x) + offsetX);
        pData->rightStick.y = ClampStick(static_cast<int>(pData->rightStick.y) + offsetY);
    }

    if (g_Mods.autoMarkR3 && isAiming) {
        if ((g_FrameCounter % AUTO_MARK_INTERVAL) < AUTO_MARK_HOLD) {
            pData->buttons |= ORBIS_PAD_BUTTON_R3;
        }
    }

    return ret;
}

extern "C" __attribute__((visibility("default"))) int module_start(size_t argc, const void* argv) {
    Detour_scePadReadState.DetourFunction(
        reinterpret_cast<uintptr_t>(scePadReadState),
        reinterpret_cast<void*>(scePadReadState_Hook)
    );
    Notify("TLOU Pad PRX Carregado! [Quadrado + Direita]");
    return 0;
}

extern "C" __attribute__((visibility("default"))) int module_stop(size_t argc, const void* argv) {
    Detour_scePadReadState.RestoreFunction();
    return 0;
}
