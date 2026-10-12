/* Reuse upstream identity-based message/save banner OAM tagging. */
#include "../../3ds/source/port_overlay_text_oam.c"

static uint32_t minimap_mask[4];
static uint32_t world_mask[4];
static int world_origin[128];
void SwitchWorld_BeginFrame(void) { for (int i=0;i<4;++i) world_mask[i]=0; }
void SwitchWorld_NoteSlots(int first,int end,int origin) {
    for (int i=first;i<end && i<128;++i) if (i>=0) {
        world_mask[i>>5]|=(uint32_t)1u<<(i&31); world_origin[i]=origin;
    }
}
int SwitchWorld_Position(int slot,int raw) {
    if (slot<0 || slot>=128 || !(world_mask[slot>>5]&((uint32_t)1u<<(slot&31)))) return -32768;
    int origin=world_origin[slot];
    return origin+(((raw-origin+256)&511)-256);
}
void SwitchHud_BeginFrame(void) { for (int i=0;i<4;++i) minimap_mask[i]=0; }
void SwitchHud_NoteMinimap(int slot) { if (slot>=0 && slot<128) minimap_mask[slot>>5]|=(uint32_t)1u<<(slot&31); }
int SwitchHud_IsMinimap(int slot) { return slot>=0 && slot<128 ? (minimap_mask[slot>>5]>>(slot&31))&1u : 0; }
