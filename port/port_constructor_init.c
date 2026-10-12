/*
 * All of these Init_* functions used to be __attribute__((constructor))
 * -- run automatically before main(), by the C runtime. That's too early:
 * they copy ROM-data pointers (macros resolved by PortGen_All_Init(), which
 * itself only runs once Port_LoadRom() is called from main()) into static
 * pointer-table arrays. Running before ROM load meant they permanently
 * captured NULL, causing a null-deref crash the first time any of these
 * tables got indexed (found via src/menus/pause_screen.c's
 * sMaintainedInputDelaysPointers, see docs/3ds-port-status-2026-08-17.md).
 * Called once from Port_LoadRom(), right after PortGen_All_Init(), instead.
 *
 * Declared weak + null-checked because several of these (mostly the
 * per-language src/data/text_data.c ones) only exist for specific
 * REGION_EU/REGION_US/REGION_JP builds -- easier to no-op the ones that
 * don't exist in a given build than to duplicate each file's exact ifdef
 * condition here.
 */

#include "port_constructor_init.h"
#include "constants/text.h"
#include "location_text.h"
#include "port_gba_mem.h"
#include "data/text_data.h"
#include "constants/color_fading.h"
#include "data/color_fading_data.h"
#include "data/cutscenes/cutscenes_data.h"
#include "data/in_game_cutscene_data.h"
#include "port/generated/empty_datatypes_rom.h"




extern void Init_sEraseSramTextGfxPointers(void) __attribute__((weak));
extern void Init_sTitleScreenPointers(void) __attribute__((weak));
extern void Init_sGameOverTextPromptGfxPointers(void) __attribute__((weak));
extern void Init_sFileSelectTextGfxPointers(void) __attribute__((weak));
extern void Init_sFileSelectLargeTextGfxPointers(void) __attribute__((weak));
extern void Init_sFileSelectDifficultyTextGfxPointers(void) __attribute__((weak));
extern void Init_sMapScreenGfxPointers(void) __attribute__((weak));
extern void Init_sMapScreenChozoStatueAreaNamesGfxPointers(void) __attribute__((weak));
extern void Init_sMapScreenUnknownItemsNamesGfxPointers(void) __attribute__((weak));
extern void Init_sMapScreenEquipmentNamesGfxPointers(void) __attribute__((weak));
extern void Init_sMapScreenMenuNamesGfxPointers(void) __attribute__((weak));
extern void Init_sStatusScreenFlagsOrderPointers(void) __attribute__((weak));
extern void Init_sMinimapDataPointers(void) __attribute__((weak));
extern void Init_sMaintainedInputDelaysPointers(void) __attribute__((weak));
extern void Init_sBootDebugCutsceneBTextPointers(void) __attribute__((weak));
extern void Init_sPauseDebugEventNamePointers(void) __attribute__((weak));
extern void Init_sChozoStatueTargetPathPointers(void) __attribute__((weak));
extern void Init_sSpritesGfxPalPointers(void) __attribute__((weak));
extern void Init_sSpritesPalettePointers(void) __attribute__((weak));
extern void Init_sSpritesetPointers(void) __attribute__((weak));
extern void Init_sDemoRamDataPointers(void) __attribute__((weak));
extern void Init_sBackgroundEffectBehaviorPointers(void) __attribute__((weak));
extern void Init_sMotherBrainFrameDataPointers(void) __attribute__((weak));
extern void Init_sRidleyFrameDataPointers(void) __attribute__((weak));
extern void Init_sCrocomireFrameDataPointers(void) __attribute__((weak));
extern void Init_sKraidFrameDataPointers(void) __attribute__((weak));
extern void Init_sImagoCocoonFrameDataPointers(void) __attribute__((weak));
extern void Init_sImagoLarvaFrameDataPointers(void) __attribute__((weak));
extern void Init_sTangleVineFrameDataPointers(void) __attribute__((weak));
extern void Init_sChozoStatueFrameDataPointers(void) __attribute__((weak));
extern void Init_sImagoFrameDataPointers(void) __attribute__((weak));
extern void Init_sUnknownItemChozoStatueFrameDataPointers(void) __attribute__((weak));
extern void Init_sMechaRidleyFrameDataPointers(void) __attribute__((weak));
extern void Init_sDescriptionTextPointers(void) __attribute__((weak));
extern void Init_sHatchLockEventsPointers(void) __attribute__((weak));
extern void Init_sJapaneseTextPointers_Message(void) __attribute__((weak));
extern void Init_sJapaneseTextPointers_Location(void) __attribute__((weak));
extern void Init_sHiraganaTextPointers_Message(void) __attribute__((weak));
extern void Init_sHiraganaTextPointers_Location(void) __attribute__((weak));
extern void Init_sEnglishTextPointers_Message(void) __attribute__((weak));
extern void Init_sEnglishTextPointers_Location(void) __attribute__((weak));
extern void Init_sJapaneseTextPointers_Description(void) __attribute__((weak));
extern void Init_sJapaneseTextPointers_Story(void) __attribute__((weak));
extern void Init_sJapaneseTextPointers_FileScreen(void) __attribute__((weak));
extern void Init_sHiraganaTextPointers_Description(void) __attribute__((weak));
extern void Init_sHiraganaTextPointers_Story(void) __attribute__((weak));
extern void Init_sHiraganaTextPointers_FileScreen(void) __attribute__((weak));
extern void Init_sEnglishTextPointers_Description(void) __attribute__((weak));
extern void Init_sEnglishTextPointers_Story(void) __attribute__((weak));
extern void Init_sEnglishTextPointers_FileScreen(void) __attribute__((weak));
extern void Init_sGermanTextPointers_Description(void) __attribute__((weak));
extern void Init_sGermanTextPointers_Story(void) __attribute__((weak));
extern void Init_sFrenchTextPointers_Description(void) __attribute__((weak));
extern void Init_sFrenchTextPointers_Story(void) __attribute__((weak));
extern void Init_sItalianTextPointers_Description(void) __attribute__((weak));
extern void Init_sItalianTextPointers_Story(void) __attribute__((weak));
extern void Init_sSpanishTextPointers_Description(void) __attribute__((weak));
extern void Init_sSpanishTextPointers_Story(void) __attribute__((weak));
extern void Init_sPlayerNumbersStringPointers(void) __attribute__((weak));
extern void Init_sStoryTextPointers(void) __attribute__((weak));
extern void Init_sAreaPointers(void) __attribute__((weak));
extern void Init_sAreaRoomEntryPointers(void) __attribute__((weak));
extern void Init_sScrollTables(void) __attribute__((weak));
extern void Init_sPauseScreenPointers(void) __attribute__((weak));

/*
 * Text pointer tables in ROM contain 32-bit raw GBA addresses (both for the
 * language subtables and the message strings themselves). In host memory,
 * indexing these as 2D pointer arrays directly will dereference GBA addresses
 * as host pointers, causing unaligned reads or accessing unmapped/heap memory
 * (e.g. 0x55555555 on 3DS). We populate RAM tables with fully resolved host
 * pointers at ROM load time.
 */
static const u16* sLocationTextPointers_Ram[LANGUAGE_COUNT][LT_COUNT];
static const u16* sMessageTextPointers_Ram[LANGUAGE_COUNT][MESSAGE_COUNT];
static const u16* sFileScreenTextPointers_Ram[LANGUAGE_COUNT][FILE_SCREEN_TEXT_COUNT];
static const u16* sStoryTextPointers_Ram[LANGUAGE_COUNT][STORY_TEXT_COUNT];
static const u16* sDescriptionTextPointers_Ram[LANGUAGE_COUNT][DESCRIPTION_TEXT_COUNT];

static const u16* sLocationTextLangPointers_Ram[LANGUAGE_COUNT][LT_COUNT];
static const u16* sMessageTextLangPointers_Ram[LANGUAGE_COUNT][MESSAGE_COUNT];
static const u16* sFileScreenTextLangPointers_Ram[LANGUAGE_COUNT][FILE_SCREEN_TEXT_COUNT];

static const u16* const* sLocationTextRoot_Ram[LANGUAGE_COUNT];
static const u16* const* sMessageTextRoot_Ram[LANGUAGE_COUNT];
static const u16* const* sFileScreenTextRoot_Ram[LANGUAGE_COUNT];

#ifdef PLATFORM_SWITCH
typedef uintptr_t PortTextPointer;
#else
typedef uint32_t PortTextPointer;
#endif

static void Port_Build2DTextTable(void* pRomRawTable, const u16* pRamTable[][1], const u16* const** pRootTable, u32 count)
{
    u32 l, m;
    const PortTextPointer* romLangs;
    if (!pRomRawTable) return;
    romLangs = (const PortTextPointer*)pRomRawTable;
    for (l = 0; l < LANGUAGE_COUNT; l++) {
        const u16** ramSubTable = (const u16**)((uintptr_t)pRamTable + l * count * sizeof(const u16*));
        PortTextPointer langGbaAddr;
#ifndef PLATFORM_SWITCH
        const PortTextPointer* romMsgPointers;
#endif
        pRootTable[l] = ramSubTable;
        langGbaAddr = romLangs[l];
#ifdef PLATFORM_SWITCH
        /* Language rows in the message/location roots are anonymous ROM
         * tables. Their entries are always serialized 32-bit GBA addresses,
         * even when another named table at that address has been expanded. */
        if (gRomData && langGbaAddr >= 0x08000000u &&
            langGbaAddr - 0x08000000u <= gRomSize &&
            (size_t)count * 4 <= gRomSize - (langGbaAddr - 0x08000000u)) {
            const u8* raw = gRomData + langGbaAddr - 0x08000000u;
            for (m = 0; m < count; m++) {
                const u8* entry = raw + m * 4;
                u32 textGbaAddr = (u32)entry[0] | (u32)entry[1] << 8 |
                                  (u32)entry[2] << 16 | (u32)entry[3] << 24;
                ramSubTable[m] = (const u16*)port_resolve_addr(textGbaAddr);
            }
        }
#else
        romMsgPointers = (const PortTextPointer*)port_resolve_addr(langGbaAddr);
        if (romMsgPointers) {
            for (m = 0; m < count; m++) {
                PortTextPointer textGbaAddr = romMsgPointers[m];
                ramSubTable[m] = (const u16*)port_resolve_addr(textGbaAddr);
            }
        }
#endif
    }
}

static void Port_Build1DTextTable(void* pRomRawTable, const u16** pRamTable, u32 count)
{
    u32 m;
    const PortTextPointer* romMsgPointers;
    if (!pRomRawTable) return;
    romMsgPointers = (const PortTextPointer*)pRomRawTable;
    for (m = 0; m < count; m++) {
        PortTextPointer textGbaAddr = romMsgPointers[m];
        pRamTable[m] = (const u16*)port_resolve_addr(textGbaAddr);
    }
}


static void Port_InitTextPointerTables(void)
{
    // Build 2D tables: FileScreen, Message, Location
    Port_Build2DTextTable((void*)p_sFileScreenTextPointers, (const u16* (*)[1])sFileScreenTextPointers_Ram, sFileScreenTextRoot_Ram, FILE_SCREEN_TEXT_COUNT);
    p_sFileScreenTextPointers = (const u16**(*)[LANGUAGE_COUNT])&sFileScreenTextRoot_Ram;

    Port_Build2DTextTable((void*)p_sMessageTextPointers, (const u16* (*)[1])sMessageTextPointers_Ram, sMessageTextRoot_Ram, MESSAGE_COUNT);
    p_sMessageTextPointers = (const u16**(*)[LANGUAGE_COUNT])&sMessageTextRoot_Ram;

    Port_Build2DTextTable((void*)p_sLocationTextPointers, (const u16* (*)[1])sLocationTextPointers_Ram, sLocationTextRoot_Ram, LT_COUNT);
    p_sLocationTextPointers = (const u16**(*)[LANGUAGE_COUNT])&sLocationTextRoot_Ram;


    // Per-language FileScreen sub-tables
    p_sJapaneseTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_JAPANESE];
    p_sHiraganaTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_HIRAGANA];
    p_sEnglishTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_ENGLISH];
#if defined(REGION_EU)
    p_sGermanTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_GERMAN];
    p_sFrenchTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_FRENCH];
    p_sItalianTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_ITALIAN];
    p_sSpanishTextPointers_FileScreen = (const u16*(*)[FILE_SCREEN_TEXT_COUNT])sFileScreenTextPointers_Ram[LANGUAGE_SPANISH];
#endif

    // Per-language Story tables
    Port_Build1DTextTable((void*)p_sJapaneseTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_JAPANESE], STORY_TEXT_COUNT);
    p_sJapaneseTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_JAPANESE];

    Port_Build1DTextTable((void*)p_sHiraganaTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_HIRAGANA], STORY_TEXT_COUNT);
    p_sHiraganaTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_HIRAGANA];

    Port_Build1DTextTable((void*)p_sEnglishTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_ENGLISH], STORY_TEXT_COUNT);
    p_sEnglishTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_ENGLISH];

#if defined(REGION_EU)
    Port_Build1DTextTable((void*)p_sGermanTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_GERMAN], STORY_TEXT_COUNT);
    p_sGermanTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_GERMAN];

    Port_Build1DTextTable((void*)p_sFrenchTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_FRENCH], STORY_TEXT_COUNT);
    p_sFrenchTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_FRENCH];

    Port_Build1DTextTable((void*)p_sItalianTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_ITALIAN], STORY_TEXT_COUNT);
    p_sItalianTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_ITALIAN];

    Port_Build1DTextTable((void*)p_sSpanishTextPointers_Story, sStoryTextPointers_Ram[LANGUAGE_SPANISH], STORY_TEXT_COUNT);
    p_sSpanishTextPointers_Story = (const u16*(*)[STORY_TEXT_COUNT])sStoryTextPointers_Ram[LANGUAGE_SPANISH];
#endif

    // Per-language Description tables
    Port_Build1DTextTable((void*)p_sJapaneseTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_JAPANESE], DESCRIPTION_TEXT_COUNT);
    p_sJapaneseTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_JAPANESE];

    Port_Build1DTextTable((void*)p_sHiraganaTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_HIRAGANA], DESCRIPTION_TEXT_COUNT);
    p_sHiraganaTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_HIRAGANA];

    Port_Build1DTextTable((void*)p_sEnglishTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_ENGLISH], DESCRIPTION_TEXT_COUNT);
    p_sEnglishTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_ENGLISH];

#if defined(REGION_EU)
    Port_Build1DTextTable((void*)p_sGermanTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_GERMAN], DESCRIPTION_TEXT_COUNT);
    p_sGermanTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_GERMAN];

    Port_Build1DTextTable((void*)p_sFrenchTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_FRENCH], DESCRIPTION_TEXT_COUNT);
    p_sFrenchTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_FRENCH];

    Port_Build1DTextTable((void*)p_sItalianTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_ITALIAN], DESCRIPTION_TEXT_COUNT);
    p_sItalianTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_ITALIAN];

    Port_Build1DTextTable((void*)p_sSpanishTextPointers_Description, sDescriptionTextPointers_Ram[LANGUAGE_SPANISH], DESCRIPTION_TEXT_COUNT);
    p_sSpanishTextPointers_Description = (const u16*(*)[DESCRIPTION_TEXT_COUNT])sDescriptionTextPointers_Ram[LANGUAGE_SPANISH];
#endif
}

void Port_InitConstructorPointers(void)
{
    Port_InitTextPointerTables();

    if (Init_sEraseSramTextGfxPointers) Init_sEraseSramTextGfxPointers();
    if (Init_sTitleScreenPointers) Init_sTitleScreenPointers();
    if (Init_sGameOverTextPromptGfxPointers) Init_sGameOverTextPromptGfxPointers();
    if (Init_sFileSelectTextGfxPointers) Init_sFileSelectTextGfxPointers();
    if (Init_sFileSelectLargeTextGfxPointers) Init_sFileSelectLargeTextGfxPointers();
    if (Init_sFileSelectDifficultyTextGfxPointers) Init_sFileSelectDifficultyTextGfxPointers();
    if (Init_sMapScreenGfxPointers) Init_sMapScreenGfxPointers();
    if (Init_sMapScreenChozoStatueAreaNamesGfxPointers) Init_sMapScreenChozoStatueAreaNamesGfxPointers();
    if (Init_sMapScreenUnknownItemsNamesGfxPointers) Init_sMapScreenUnknownItemsNamesGfxPointers();
    if (Init_sMapScreenEquipmentNamesGfxPointers) Init_sMapScreenEquipmentNamesGfxPointers();
    if (Init_sMapScreenMenuNamesGfxPointers) Init_sMapScreenMenuNamesGfxPointers();
    if (Init_sStatusScreenFlagsOrderPointers) Init_sStatusScreenFlagsOrderPointers();
    if (Init_sMinimapDataPointers) Init_sMinimapDataPointers();
    if (Init_sMaintainedInputDelaysPointers) Init_sMaintainedInputDelaysPointers();
    if (Init_sBootDebugCutsceneBTextPointers) Init_sBootDebugCutsceneBTextPointers();
    if (Init_sPauseDebugEventNamePointers) Init_sPauseDebugEventNamePointers();
    if (Init_sChozoStatueTargetPathPointers) Init_sChozoStatueTargetPathPointers();
    if (Init_sSpritesGfxPalPointers) Init_sSpritesGfxPalPointers();
    if (Init_sSpritesPalettePointers) Init_sSpritesPalettePointers();
    if (Init_sSpritesetPointers) Init_sSpritesetPointers();
    if (Init_sDemoRamDataPointers) Init_sDemoRamDataPointers();
    if (Init_sBackgroundEffectBehaviorPointers) Init_sBackgroundEffectBehaviorPointers();
    if (Init_sMotherBrainFrameDataPointers) Init_sMotherBrainFrameDataPointers();
    if (Init_sRidleyFrameDataPointers) Init_sRidleyFrameDataPointers();
    if (Init_sCrocomireFrameDataPointers) Init_sCrocomireFrameDataPointers();
    if (Init_sKraidFrameDataPointers) Init_sKraidFrameDataPointers();
    if (Init_sImagoCocoonFrameDataPointers) Init_sImagoCocoonFrameDataPointers();
    if (Init_sImagoLarvaFrameDataPointers) Init_sImagoLarvaFrameDataPointers();
    if (Init_sTangleVineFrameDataPointers) Init_sTangleVineFrameDataPointers();
    if (Init_sChozoStatueFrameDataPointers) Init_sChozoStatueFrameDataPointers();
    if (Init_sImagoFrameDataPointers) Init_sImagoFrameDataPointers();
    if (Init_sUnknownItemChozoStatueFrameDataPointers) Init_sUnknownItemChozoStatueFrameDataPointers();
    if (Init_sMechaRidleyFrameDataPointers) Init_sMechaRidleyFrameDataPointers();
    if (Init_sDescriptionTextPointers) Init_sDescriptionTextPointers();
    if (Init_sHatchLockEventsPointers) Init_sHatchLockEventsPointers();
    if (Init_sJapaneseTextPointers_Message) Init_sJapaneseTextPointers_Message();
    if (Init_sJapaneseTextPointers_Location) Init_sJapaneseTextPointers_Location();
    if (Init_sHiraganaTextPointers_Message) Init_sHiraganaTextPointers_Message();
    if (Init_sHiraganaTextPointers_Location) Init_sHiraganaTextPointers_Location();
    if (Init_sEnglishTextPointers_Message) Init_sEnglishTextPointers_Message();
    if (Init_sEnglishTextPointers_Location) Init_sEnglishTextPointers_Location();
    if (Init_sJapaneseTextPointers_Description) Init_sJapaneseTextPointers_Description();
    if (Init_sJapaneseTextPointers_Story) Init_sJapaneseTextPointers_Story();
    if (Init_sJapaneseTextPointers_FileScreen) Init_sJapaneseTextPointers_FileScreen();
    if (Init_sHiraganaTextPointers_Description) Init_sHiraganaTextPointers_Description();
    if (Init_sHiraganaTextPointers_Story) Init_sHiraganaTextPointers_Story();
    if (Init_sHiraganaTextPointers_FileScreen) Init_sHiraganaTextPointers_FileScreen();
    if (Init_sEnglishTextPointers_Description) Init_sEnglishTextPointers_Description();
    if (Init_sEnglishTextPointers_Story) Init_sEnglishTextPointers_Story();
    if (Init_sEnglishTextPointers_FileScreen) Init_sEnglishTextPointers_FileScreen();
    if (Init_sGermanTextPointers_Description) Init_sGermanTextPointers_Description();
    if (Init_sGermanTextPointers_Story) Init_sGermanTextPointers_Story();
    if (Init_sFrenchTextPointers_Description) Init_sFrenchTextPointers_Description();
    if (Init_sFrenchTextPointers_Story) Init_sFrenchTextPointers_Story();
    if (Init_sItalianTextPointers_Description) Init_sItalianTextPointers_Description();
    if (Init_sItalianTextPointers_Story) Init_sItalianTextPointers_Story();
    if (Init_sSpanishTextPointers_Description) Init_sSpanishTextPointers_Description();
    if (Init_sSpanishTextPointers_Story) Init_sSpanishTextPointers_Story();
    if (Init_sPlayerNumbersStringPointers) Init_sPlayerNumbersStringPointers();
    if (Init_sStoryTextPointers) Init_sStoryTextPointers();
    if (Init_sAreaPointers) Init_sAreaPointers();
    if (Init_sAreaRoomEntryPointers) Init_sAreaRoomEntryPointers();
    if (Init_sScrollTables) Init_sScrollTables();
    if (Init_sPauseScreenPointers) Init_sPauseScreenPointers();

    extern const struct ColorFadingData sColorFadingData_Compiled[COLOR_FADING_COUNT];
    p_sColorFadingData = (const struct ColorFadingData(*)[COLOR_FADING_COUNT])sColorFadingData_Compiled;

    extern const struct ColorFadingColorInfo sColorFadingColorInfo_Compiled[COLOR_FADING_SPEED_COUNT];
    p_sColorFadingColorInfo = (const struct ColorFadingColorInfo(*)[5])sColorFadingColorInfo_Compiled;

    extern const struct CutsceneInfo sCutsceneData_Compiled[CUTSCENE_COUNT];
    p_sCutsceneData = (const struct CutsceneInfo(*)[CUTSCENE_COUNT])sCutsceneData_Compiled;

    extern const struct InGameCutsceneData sInGameCutsceneData_Compiled[IGC_COUNT];
    p_sInGameCutsceneData = (const struct InGameCutsceneData(*)[IGC_COUNT])sInGameCutsceneData_Compiled;

    extern const struct HatchData sHatchData_Empty_Compiled;
    p_sHatchData_Empty = &sHatchData_Empty_Compiled;

    extern const struct Coordinates sCoordsX_Empty_Compiled;
    p_sCoordsX_Empty = &sCoordsX_Empty_Compiled;

    extern const struct LockScreen sLockScreen_Empty_Compiled;
    p_sLockScreen_Empty = &sLockScreen_Empty_Compiled;

    extern const struct BackgroundEffect sBackgroundEffect_Empty_Compiled;
    p_sBackgroundEffect_Empty = &sBackgroundEffect_Empty_Compiled;

    extern const struct BrokenBlock sBrokenBlock_Empty_Compiled;
    p_sBrokenBlock_Empty = &sBrokenBlock_Empty_Compiled;

    extern const struct BombChain sBombChain_Empty_Compiled;
    p_sBombChain_Empty = &sBombChain_Empty_Compiled;

    extern const struct PowerBomb sPowerBomb_Empty_Compiled;
    p_sPowerBomb_Empty = &sPowerBomb_Empty_Compiled;

    extern const struct ScreenShake sScreenShake_Empty_Compiled;
    p_sScreenShake_Empty = &sScreenShake_Empty_Compiled;

    extern const struct BG0Movement sBg0Movement_Empty_Compiled;
    p_sBg0Movement_Empty = &sBg0Movement_Empty_Compiled;

    extern const struct BG3Movement sBg3Movement_Empty_Compiled;
    p_sBg3Movement_Empty = &sBg3Movement_Empty_Compiled;

    extern const struct Unused_3005504 sUnusedStruct_3005504_Empty_Compiled;
    p_sUnusedStruct_3005504_Empty = &sUnusedStruct_3005504_Empty_Compiled;

    extern const struct WaterMovement sWaterMovement_Empty_Compiled;
    p_sWaterMovement_Empty = &sWaterMovement_Empty_Compiled;

    extern const struct TransparencyRelated sTransparencyRelated_Empty_Compiled;
    p_sTransparencyRelated_Empty = &sTransparencyRelated_Empty_Compiled;

    extern const struct LastElevatorUsed sLastElevatorUsed_Empty_Compiled;
    p_sLastElevatorUsed_Empty = &sLastElevatorUsed_Empty_Compiled;

    extern const struct BldalphaData sBldalphaData_Empty_Compiled;
    p_sBldalphaData_Empty = &sBldalphaData_Empty_Compiled;

    extern const struct BldyData sBldyData_Empty_Compiled;
    p_sBldyData_Empty = &sBldyData_Empty_Compiled;

    extern const struct DefaultTransparency sDefaultTransparency_Empty_Compiled;
    p_sDefaultTransparency_Empty = &sDefaultTransparency_Empty_Compiled;
}





