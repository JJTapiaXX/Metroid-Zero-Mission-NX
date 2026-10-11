#ifndef PORT_AUTO_SPACE_JUMP_H
#define PORT_AUTO_SPACE_JUMP_H

#include <stdbool.h>

/* Automatic Space Jump. With Space Jump equipped, the game lets Samus jump again in the air while
 * she spins, but only on a *fresh* A press made while she is already falling
 * (gChangedInput & KEY_A with yVelocity <= -SUB_PIXEL_TO_VELOCITY(EIGHTH_BLOCK_SIZE), in
 * SamusSpinning, src/samus.c): the player has to tap A in rhythm. With AUTO SPACE JUMP on, holding
 * A is enough: the assist lifts the button on the frames before the game would accept the next
 * jump and presses it on the first frame it does, so the spin climbs as fast as the game allows.
 *
 * The option (mzm3ds.ini `auto_space_jump`, OPTIONS -> CONTROLS). On by default; off in RA
 * hardcore, like the other input assists. */
bool Port_AutoSpaceJump_Enabled(void);
void Port_AutoSpaceJump_SetEnabled(bool on);

/* Called once per game frame from the key poll, right before the frame runs. `jumpHeld`: a button
 * mapped to JUMP is held; `aInput`: KEY_A as the player's buttons give it this frame. Returns true
 * when the assist takes over KEY_A this frame, with *press the value to send. */
bool Port_AutoSpaceJump_Translate(bool jumpHeld, bool aInput, bool* press);

#endif
