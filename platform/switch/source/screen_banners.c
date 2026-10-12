/* Reuse upstream identity-based message/save banner OAM tagging. */
#include "../../3ds/source/port_overlay_text_oam.c"

static uint32_t minimap_mask[4];
void SwitchHud_BeginFrame(void) { for (int i=0;i<4;++i) minimap_mask[i]=0; }
void SwitchHud_NoteMinimap(int slot) { if (slot>=0 && slot<128) minimap_mask[slot>>5]|=(uint32_t)1u<<(slot&31); }
int SwitchHud_IsMinimap(int slot) { return slot>=0 && slot<128 ? (minimap_mask[slot>>5]>>(slot&31))&1u : 0; }
