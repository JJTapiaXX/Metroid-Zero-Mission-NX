/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "game_switch.h"
#include "wide_view.h"
#include "platform_switch.h"
#include "switch_ui.h"
#include "port_rom.h"
#include "port_gba_mem.h"
#include "virtuappu.h"
#include "cpu/mode1.h"
#include "gba.h"
#include "structs/game_state.h"
#include "port_psg_synth.h"
#include "audio_volume.h"
#include "audio_filters.h"
#include "achievements_switch.h"
#include "audio_switch.h"
#include "diagnostics.h"
#include "structs/audio.h"
#include "structs/samus.h"
#include "structs/connection.h"
#include <stdbool.h>

extern void agbmain(void);
extern void CallbackCallVblank(void);
extern void Port_LoadSram(void);
extern void Port_FlushSramWait(void);
extern void Port_SaveSram(void);
static bool quit_requested;
static uint64_t last_resume_ns;

bool GameSwitch_Load(const char *path) {
    if (!Port_LoadRom(path)) return false;
    Port_LoadSram();
    AudioVolume_Load();
    AudioFilters_Load();
    SwitchOverlay_LoadOptions();
    SwitchUI_PrepareAssets();
    /* Native frame rendering has no DMA scanline hazard; report safe VBlank. */
    GameSwitch_Scanline(161);
    PortPsg_Reset();
    virtuappu_reset();
    VirtuaPPUMode1GbaMemory memory = {gIoMem, gVram, gBgPltt, gObjPltt, gOamMem};
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_registers.frame_width = 240;
    virtuappu_registers.frame_pitch = 240;
    Achievements_Init();
    return true;
}

void GameSwitch_RequestQuit(void) { quit_requested = true; }
void GameSwitch_Run(void) {
    AudioSwitch_LockState(); agbmain(); AudioSwitch_UnlockState();
}
void GameSwitch_AudioTick(void) {
    if (gMusicInfo.sampleRate) {
        extern void UpdateAudio(void);
        UpdateAudio();
    }
}
void GameSwitch_Shutdown(void) {
    Achievements_Close();
    Port_FlushSramWait(); Port_SaveSram(); AudioVolume_Save(); AudioFilters_Save(); SwitchOverlay_SaveOptions();
}

void GameSwitch_Scanline(unsigned line) {
    gba_write8((uint32_t)(uintptr_t)REG_VCOUNT, (uint8_t)line);
    uint16_t status = gba_read16((uint32_t)(uintptr_t)REG_DISPSTAT);
    if (line >= 160) status |= DSTAT_IN_VBLANK;
    else status &= ~DSTAT_IN_VBLANK;
    gba_write16((uint32_t)(uintptr_t)REG_DISPSTAT, status);
}

void GameSwitch_VBlank(void) {
    uint64_t entry_ns=AudioSwitch_NowNs();
    if (gMainGameMode==GM_INGAME && gSubGameMode1==2) Achievements_Evaluate();
    unsigned logic_time=last_resume_ns ? (entry_ns-last_resume_ns)/1000 : 0;
    AudioSwitch_UnlockState();
    Diagnostics_Checkpoint(1);
    SwitchSurface main_screen = {virtuappu_frame_buffer, (int)virtuappu_registers.frame_width, 160, virtuappu_registers.frame_pitch};
    SwitchInput input;
    bool had_focus=false;
    do {
        Achievements_Update();
        input = PlatformSwitch_Poll();
        if (input.quit || quit_requested) {
            AudioSwitch_LockState();
            gResetGame = TRUE; gVBlankRequestFlag = TRUE; return;
        }
        Diagnostics_Frame(gMainGameMode,gSubGameMode1,gCurrentArea,gCurrentRoom,
            gSamusData.pose,gSamusData.xPosition,gSamusData.yPosition,input.overlay_visible);
        if (input.overlay_visible && !SwitchOverlay_Layout()) {
            had_focus=true;
            SwitchSurface second = {SwitchUI_Render(&input), 320, 240, 320};
            PlatformSwitch_Present(&main_screen, &second);
        }
    } while (input.overlay_visible && !SwitchOverlay_Layout());
    gba_write16((uint32_t)(uintptr_t)REG_KEY_INPUT, (~input.gba_keys) & 0x03ff);
    AudioSwitch_LockState();
    Diagnostics_Checkpoint(2);
    GameSwitch_Scanline(161);
    CallbackCallVblank();
    AudioSwitch_UnlockState();
    Diagnostics_Checkpoint(3);
    uint64_t ppu_start=AudioSwitch_NowNs();
    virtuappu_registers.mode = (gba_read16((uint32_t)(uintptr_t)REG_DISPCNT) & 7) ? 2 : 1;
    SwitchWide_Prepare();
    extern void SwitchWide_CenterBegin(void);
    extern void SwitchWide_CenterEnd(void);
    SwitchWide_CenterBegin();
    virtuappu_render_frame();
    SwitchWide_CenterEnd();
    main_screen.width=(int)virtuappu_registers.frame_width; main_screen.pitch=virtuappu_registers.frame_pitch;
    uint64_t ui_start=AudioSwitch_NowNs();
    Diagnostics_Checkpoint(4);
    const uint32_t *ui=NULL;
    if (SwitchOverlay_Layout() || had_focus) ui=SwitchUI_Render(&input);
    SwitchSurface second = {ui, 320, 240, 320};
    uint64_t present_start=AudioSwitch_NowNs();
    PlatformSwitch_Present(&main_screen, &second);
    uint64_t end_ns=AudioSwitch_NowNs();
    Diagnostics_Performance(had_focus ? 0 : logic_time,(ui_start-ppu_start)/1000,
        (present_start-ui_start)/1000,(end_ns-present_start)/1000);
    Diagnostics_Checkpoint(5);
    AudioSwitch_LockState();
    last_resume_ns=AudioSwitch_NowNs();
}

/* Optional 3DS-only integrations. The Switch bring-up build has no RA/state
 * service; these callbacks keep the existing game-facing hooks linkable. */
void Port_SaveState_ServicePending(void) { }
void Port_SaveFile_ShowFailed(void) { SwitchUI_Notice("SAVE FAILED"); }
void Port_NesMetroid_ShowUnavailable(void) { SwitchUI_Notice("NES MODE UNAVAILABLE"); }
void Port_FusionLink_ShowUnavailable(void) { SwitchUI_Notice("FUSION LINK UNAVAILABLE"); }
void Port_LanguageSelect_BottomHint(void) { }
int Port_WideMarginSubPixelX(void) { extern int SwitchWide_Margin(void); return SwitchWide_Margin()*2; }
int Port_WideMarginSubPixelY(void) { return 0; }
void Port_Wide_NoteSlots(int first, int end, int y, int x) {
    (void)first; (void)end; (void)y; (void)x;
}
bool PortPpuMzm_DebugGetForceEffect(unsigned bit) { (void)bit; return false; }
