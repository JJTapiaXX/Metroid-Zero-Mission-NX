#include "port_auto_space_jump.h"

#include "constants/game_state.h"
#include "constants/samus.h"
#include "structs/samus.h"
#include "types.h"

extern u8 gMainGameMode;
extern u8 gSubGameMode1;
extern bool Port_RA_IsHardcore(void);

/* -SUB_PIXEL_TO_VELOCITY(EIGHTH_BLOCK_SIZE): the speed of fall from which the game accepts the jump. */
#define SPACE_JUMP_FALL_VELOCITY (-SUB_PIXEL_TO_VELOCITY(EIGHTH_BLOCK_SIZE))

static bool sEnabled = true;
static int sHeld;     /* frames the jump button has been held in a row */
static bool sSent;    /* A went to the game last frame */

bool Port_AutoSpaceJump_Enabled(void) { return sEnabled; }
void Port_AutoSpaceJump_SetEnabled(bool on) { sEnabled = on; }

static bool Spinning(void) {
    switch (gSamusData.pose) {
        case SPOSE_SPINNING:
        case SPOSE_SPACE_JUMPING:
        case SPOSE_SCREW_ATTACKING:
            return true;
        default:
            return false;
    }
}

bool Port_AutoSpaceJump_Translate(bool jumpHeld, bool aInput, bool* press) {
    sHeld = jumpHeld ? sHeld + 1 : 0;
    /* The first frame of a press is the player's own jump; from the next one on, while the button
     * is held in a spin, the presses are ours. */
    if (!sEnabled || sHeld < 2 || Port_RA_IsHardcore() || gMainGameMode != GM_INGAME ||
        gSubGameMode1 != SUB_GAME_MODE_PLAYING || !Spinning() ||
        !(gEquipment.suitMiscActivation & SMF_SPACE_JUMP) || gSamusPhysics.slowedByLiquid) {
        sSent = aInput;
        return false;
    }
    /* Going up: the button stays down, or the game cuts the jump short. Falling: up from the frame
     * the speed of fall allows the jump, a fresh press (the frame before it was released). */
    const int v = (int)gSamusData.yVelocity;
    const bool fastEnough = v <= SPACE_JUMP_FALL_VELOCITY;
    const bool rising = v > 0;
    const bool down = rising || (fastEnough && !sSent);
    sSent = down;
    *press = down;
    return true;
}
