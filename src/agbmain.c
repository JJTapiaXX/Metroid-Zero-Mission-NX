#include "syscalls.h"
#include "data/generic_data.h"
#include "data/menus/language_select_data.h"

#include "gba.h"
#include "port_debug_log.h"
#include "audio/track_internal.h"
#include "constants/game_state.h"
#include "region.h"

#include "structs/cutscene.h"
#include "structs/demo.h"
#include "structs/game_state.h"
#include "structs/display.h"
#ifdef PORT_LINUX_DIAG_WARP_TO_DEOREM
#include "structs/samus.h"
#endif

/**
 * @brief 23c | 464 | Main loop of the game
 *  
 */
void agbmain(void)
{
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
    /* Temporary boot-hang diagnostic checkpoints, see port/port_debug_log.h.
     * Gated behind PORT_VERBOSE_FRAME_LOG (off by default) since this does a
     * flushed file write per call -- ~10x/frame between this file and
     * Port_Bios_Halt was the single biggest perf cost once boot was no
     * longer hanging. Re-enable (add -DPORT_VERBOSE_FRAME_LOG to the
     * Makefile) when bisecting a new boot-time hang. */
    extern void Port_DebugLog(const char* msg);
    Port_DebugLog("agbmain: before InitializeGame()");
#endif
    InitializeGame();
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
    Port_DebugLog("agbmain: InitializeGame() done, entering main loop");
#endif

    while (TRUE)
    {
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        {
            static int sLoopCount = 0;
            if (sLoopCount < 5) {
                Port_DebugLog("agbmain: loop iteration start");
                sLoopCount++;
            }
        }
#endif
        gVblankActive = FALSE;
        if (REGION_IS_EU() && (gMainGameMode == GM_INGAME || gMainGameMode == GM_DEMO))
            InGameIoWriteRegisters();
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        Port_DebugLog("agbmain: before UpdateAudio()");
#endif
#if (defined(MZM_3DS) && defined(__3DS__)) || defined(PLATFORM_SWITCH)
        /* Audio production (the unk_10/unk_C/unk_E DMA2-IRQ-cadence
         * approximation, the SoundCodeC hook install, and UpdateAudio()
         * itself) no longer runs here. On real GBA hardware this is all
         * driven by a DMA/timer interrupt independent of the CPU's main
         * loop (see src/music_wrappers.c's DMA2IntrCode, the real interrupt
         * this counter update approximates); running it from agbmain's own
         * loop instead tied audio production to however fast this loop
         * iterates, i.e. to render FPS. It's now on its own thread, ticked
         * by the real NDSP hardware playback cadence instead -- see
         * platform/3ds/source/port_mzm_audio_3ds.c's AudioThreadMain. */
#else
        UpdateAudio();
#endif

        if (gResetGame)
            break;

        UpdateInput();
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        Port_DebugLog("agbmain: UpdateInput() done");
#endif
        SoftResetCheck();
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        Port_DebugLog("agbmain: SoftResetCheck() done");
#endif

#ifdef PORT_LINUX_DIAG_WARP_TO_DEOREM
        /* Two-stage debug warp, bypassing menu navigation entirely (test
         * mode's scripted KEY_A pulse isn't enough to get through file
         * select). Stage 1: force straight into a fresh GM_INGAME after a
         * short settle delay, skipping intro/title/file-select. Stage 2:
         * once that fresh game has finished booting into normal gameplay
         * (SUB_GAME_MODE_PLAYING), force-jump to Brinstar room 12 with the
         * first missile tank already collected and Samus standing directly
         * under Deorem, to reproduce the "boss never appears" scenario. See
         * docs/3ds-port-status-2026-08-17.md 6j. */
        {
            extern u8 gCurrentArea;
            extern u8 gCurrentRoom;
            extern void Port_DebugLog(const char* msg);
            static u32 sFrameCount = 0;
            static bool sStage1Done = false;
            static bool sStage2Done = false;
            sFrameCount++;

            if (!sStage1Done && sFrameCount >= 30)
            {
                gMainGameMode = GM_INGAME;
                gSubGameMode1 = 0;
                gSubGameMode2 = 0;
                gSubGameMode3 = 0;
                gIsLoadingFile = FALSE;
                /* Deliberately NOT setting gCurrentArea/gCurrentRoom here:
                 * RoomReset() (room.c:639-641) recomputes gCurrentRoom from
                 * gLastDoorUsed/sAreaDoorsPointers regardless, so any value
                 * set here gets clobbered immediately. Let a real cold boot
                 * (gLastDoorUsed defaults to its BSS-zero value, same as on
                 * real hardware) land wherever it lands; stage 2 below
                 * overrides both area and room properly once we're truly in
                 * gameplay. */
                sStage1Done = true;
                Port_DebugLog("PORT_LINUX_DIAG_WARP_TO_DEOREM: stage 1, forcing GM_INGAME (skipping menus)");
            }

            if (sStage1Done && !sStage2Done && gMainGameMode == GM_INGAME && gSubGameMode1 == SUB_GAME_MODE_PLAYING)
            {
                gCurrentArea = 0; /* AREA_BRINSTAR */
                gCurrentRoom = 12;
                gEquipment.maxMissiles = 5;
                gEquipment.currentMissiles = 5;
                gSamusData.xPosition = 1280; /* centered under Deorem, see deorem.c DeoremWaitingForFight */
                gSamusData.yPosition = 3800; /* room floor, adjust if Samus spawns inside geometry */
                gSubGameMode1 = 0; /* re-enter InitAndLoadGenerics -> forces RoomLoad into room 12 */
                sStage2Done = true;
                Port_DebugLog("PORT_LINUX_DIAG_WARP_TO_DEOREM: stage 2, warped to Brinstar room 12 with maxMissiles=5");
            }
        }
#endif

        // Increment frame counters
        APPLY_DELTA_TIME_INC(gFrameCounter8Bit);
        APPLY_DELTA_TIME_INC(gFrameCounter16Bit);

#if defined(MZM_3DS)
        /* Whole-machine save/load state, requested from the bottom screen's
         * ESTADO tab (and, in DEBUG_TOOLS builds, the debug warp point).
         * Applied HERE, at the top of the main loop between frames: the
         * touch handler runs mid-frame inside Port_Bios_Halt. No-op unless
         * a slot is pending and the game is really in gameplay. Debug-only
         * work lives on the port side of this call, not in src/, so debug
         * and release builds lay the decompilation out identically -- save
         * states then load in either (see port_save_state.c). */
        {
            extern void Port_SaveState_ServicePending(void);
            Port_SaveState_ServicePending();
        }
#endif

        switch (gMainGameMode)

        {
            case GM_SOFT_RESET:
                if (SoftResetHandler())
                {
                    gMainGameMode = GM_INTRO;
                    gSubGameMode1 = 0;
                }
                break;

            case GM_INTRO:
#ifdef DEBUG
                if (gChangedInput & KEY_R)
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = GM_DEBUG_MENU;
                }
                else
#endif // DEBUG
                if (IntroHandler())
                {
                    gMainGameMode = GM_TITLE;
                    gSubGameMode1 = 0;
                }
                break;

            case GM_TITLE:
#ifdef DEBUG
                if (gChangedInput & KEY_R)
                {
                    gSubGameMode1 = 0;
                    gPauseScreenFlag = 0;
                    gSubGameMode2 = 0;
                    gMainGameMode = GM_DEBUG_MENU;
                }
                else
#endif // DEBUG
                if (TitleScreenHandler())
                {
                    /* EUR clears this before the dispatch below, the other
                     * regions after it; nothing in between reads it. */
                    gSubGameMode1 = 0;
                    if (gSubGameMode2 == 1)
                    {
                        gMainGameMode = GM_FILE_SELECT;
                    }
                    else if (gSubGameMode2 == 2)
                    {
                        DemoStart();
                        gMainGameMode = GM_DEMO;
                    }
                    else if (REGION_IS_EU() && gSubGameMode2 == 3)
                    {
                        // "Language" picked on the EUR title screen
                        gMainGameMode = GM_SOFT_RESET;
                        gSubGameMode1 = sLanguageSelectGameModeSub1Values[1];
                    }
                    else
                    {
#ifdef DEBUG
                        gMainGameMode = GM_DEBUG_MENU;
#else // !DEBUG
                        gMainGameMode = GM_INTRO;
#endif // DEBUG
                    }

                    gPauseScreenFlag = 0;
                    gSubGameMode2 = 0;
                }
                break;

            case GM_FILE_SELECT:
                if (FileSelectMenuHandler())
                {
                    if (gSubGameMode2 == 1) // If continuing file
                        gMainGameMode = GM_INGAME;
                    else if (gSubGameMode2 == 2) // If starting new file
                        gMainGameMode = GM_INGAME;
                    else if (gSubGameMode2 == 4)
                        gMainGameMode = GM_FUSION_GALLERY;
                    else if (gSubGameMode2 == 5)
                        gMainGameMode = GM_GALLERY;
                    else
                        gMainGameMode = GM_INTRO;

                    gSubGameMode1 = 0;
                    gSubGameMode3 = 0;
                    gSubGameMode2 = 0;
                }
                break;

            case GM_INGAME:
                if (InGameHandler()) 
                {
                    if (gPauseScreenFlag == PAUSE_SCREEN_NONE)
                    {
                        if (gCurrentCutscene != 0)
                        {
                            gMainGameMode = GM_CUTSCENE;
                        }
                        else if (gTourianEscapeCutsceneStage != 0)
                        {
                            gMainGameMode = GM_TOURIAN_ESCAPE;
                        }
                        else
                        {
#ifdef DEBUG
                            gMainGameMode = GM_DEBUG_MENU;
#else // !DEBUG
                            gMainGameMode = GM_TITLE;
#endif // DEBUG
                            gSubGameMode1 = 0;
                        }
                    }
                    else
                    {
                        gMainGameMode = GM_MAP_SCREEN;
                    }
                }
                break;

            case GM_MAP_SCREEN:
                if (PauseScreenHandler())
                {
                    gMainGameMode = gSubGameMode2;
                    gSubGameMode2 = 0;

                    switch (gPauseScreenFlag)
                    {
                        case PAUSE_SCREEN_UNKNOWN_1:
                            gSubGameMode3 = 0;

                        case PAUSE_SCREEN_SUITLESS_ITEMS:
                            gPauseScreenFlag = PAUSE_SCREEN_NONE;
                            break;

                        case PAUSE_SCREEN_UNKNOWN_9:
                            gPauseScreenFlag = PAUSE_SCREEN_NONE;
                            gSubGameMode2 = 1;
                            break;

                        case PAUSE_SCREEN_PAUSE_OR_CUTSCENE:
                        case PAUSE_SCREEN_UNKNOWN_3:
                        case PAUSE_SCREEN_CHOZO_HINT:
                        case PAUSE_SCREEN_MAP_DOWNLOAD:
                        case PAUSE_SCREEN_FULLY_POWERED_SUIT_ITEMS:
                            break;
                    }

                    gSubGameMode1 = 0;
                }
                break;

            case GM_GAMEOVER:
                if (GameOverHandler())
                {
                    gMainGameMode = gSubGameMode2;
                    gSubGameMode1 = 0;
                    gSubGameMode2 = 0;
                }
                break;

            case GM_CHOZODIA_ESCAPE:
                if (ChozodiaEscapeHandler())
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = GM_CREDITS;
                }
                break;

            case GM_CREDITS:
                if (CreditsHandler())
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = GM_INTRO;
#ifdef DEBUG
                    if (gBootDebugActive || gDebugMode)
                        gMainGameMode = GM_DEBUG_MENU;
#endif // DEBUG
                }
                break;

            case GM_TOURIAN_ESCAPE:
                if (TourianEscapeHandler())
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = gSubGameMode2;
#ifdef DEBUG
                    if (gBootDebugActive)
                        gMainGameMode = GM_DEBUG_MENU;
#endif // DEBUG
                }
                break;

            case GM_CUTSCENE:
                if (CutsceneHandler())
                {
                    gSubGameMode1 = 0;

                    if (gPauseScreenFlag == PAUSE_SCREEN_SUITLESS_ITEMS || gPauseScreenFlag == PAUSE_SCREEN_FULLY_POWERED_SUIT_ITEMS)
                    {
                        gMainGameMode = GM_MAP_SCREEN;
                    }
                    else
                    {
                        gMainGameMode = GM_INGAME;
#ifdef DEBUG
                        if (gBootDebugActive)
                            gMainGameMode = gBootDebugActive;
#endif // DEBUG
                    }
                }
                break;

            case GM_DEMO:
                if (InGameHandler())
                {
                    if (gPauseScreenFlag == PAUSE_SCREEN_PAUSE_OR_CUTSCENE)
                    {
                        gPauseScreenFlag = PAUSE_SCREEN_NONE;
                        gSubGameMode3 = 0;
                        gSubGameMode1 = 0;
                        if (gDemoState == 0)
                        {
                            gMainGameMode = gSubGameMode2;
                            gSubGameMode2 = gCurrentDemo.endedWithInput;
                        }
                        else {
                            DemoStart();
                            gMainGameMode = GM_DEMO;
                        }
                    }
                    else
                        gMainGameMode = GM_MAP_SCREEN;
                }
                break;

            case GM_GALLERY:
                if (GalleryHandler())
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = GM_FILE_SELECT;
                }
                break;

            case GM_FUSION_GALLERY:
                if (FusionGalleryHandler())
                {
                    gSubGameMode1 = 0;
                    gMainGameMode = GM_FILE_SELECT;
                }
                break;

            case GM_START_SOFT_RESET:
                SoftReset();
                break;

            case GM_ERASE_SRAM:
                if (EraseSramHandler())
                {
                    if (gSubGameMode2 == 1)
                    {
                        gResetGame = TRUE;
                    }
                    else
                    {
                        gMainGameMode = GM_SOFT_RESET;
#ifdef DEBUG
                        if (gDebugMode)
                            gMainGameMode = GM_DEBUG_MENU;
#endif // DEBUG
                    }

                    gSubGameMode1 = 0;
                    gSubGameMode2 = 0;
                }
                break;

            case GM_DEBUG_MENU:
#ifdef DEBUG
                if (BootDebugHandler())
                {
                    gSubGameMode1 = 0;

                    switch (gSubGameMode2)
                    {
                        case 1:
                            gMainGameMode = GM_INGAME;
                            break;
                        case 2:
                            if (REGION_IS_EU() && INVALID_EU_LANGUAGE(gLanguage))
                            {
                                gMainGameMode = GM_SOFT_RESET;
                                gSubGameMode1 = sLanguageSelectGameModeSub1Values[2];
                            }
                            else
                            {
                                gMainGameMode = GM_INTRO;
                            }
                            break;
                        case 3:
                            gMainGameMode = GM_MAP_SCREEN;
                            break;
                        case 6:
                            gMainGameMode = GM_DEMO;
                            break;
                        case 8:
                            gMainGameMode = GM_CUTSCENE;
                            gSubGameMode2 = 0x10;
                            break;
                        case 7:
                            gMainGameMode = GM_TOURIAN_ESCAPE;
                            gSubGameMode2 = 0x10;
                            break;
                        case 5:
                            gMainGameMode = GM_CREDITS;
                            break;
                        case 4:
                            gMainGameMode = GM_CHOZODIA_ESCAPE;
                            break;
                        default:
                            gMainGameMode = GM_DEBUG_MENU;
                            gWrittenToBldy_NonGameplay = 0;
                            break;
                    }
                }
#else // !DEBUG
                gMainGameMode = GM_INTRO;
                gSubGameMode1 = 0;
                break;
#endif // DEBUG
                break;
        }
        

#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        Port_DebugLog("agbmain: switch done, before Halt wait");
#endif
#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
        extern void Port_RA_EvaluateTriggers(void);
        Port_RA_EvaluateTriggers();
#endif

        /* Coalesce this frame's SRAM writes into at most one SD write
         * (issue #22: save-point hitches came from a full 64 KB file write
         * per SramWrite* call). */
        {
            extern void Port_FlushSramIfDirty(void);
            Port_FlushSramIfDirty();
        }

        gVBlankRequestFlag &= ~TRUE;
        gVblankActive = TRUE;

        do {
            SYSCALL(2); /* SYS_Halt */
        } while (!(gVBlankRequestFlag & 1));
#if defined(MZM_3DS) && defined(PORT_VERBOSE_FRAME_LOG)
        Port_DebugLog("agbmain: Halt wait done, looping");
#endif
    }

#if defined(MZM_3DS) || defined(PORT_NATIVE)
    {
        extern void Port_FlushSramWait(void);
        Port_FlushSramWait();
    }
#endif
}
