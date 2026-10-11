#ifndef PORT_AUTO_BOMB_H
#define PORT_AUTO_BOMB_H

#include <stdbool.h>

/* Automatic bomb jump (after ../sm-3ds's source/auto_bomb.c). With AUTO BOMB JUMP on, holding the
 * button that lays bombs while in the morph ball chains the bombs for the player, timed so that
 * each one throws Samus up from higher than the last: a slow climb up a shaft with one finger.
 *
 * How the game lifts the ball (src/projectile.c, src/samus.c), as read from the source:
 * - the game acts on a *fresh* B press (gChangedInput), needs gSamusWeaponInfo.cooldown == 0 and
 *   fewer than PROJ_LIMIT_BOMB bombs; a bomb is laid at Samus's position after her update, goes
 *   through INIT, two spin stages of 16 frames and, on the first EXPLODING frame (33 frames after
 *   the one it was laid in), looks once at whether Samus is within its box;
 * - from the ground any overlap bounces her; in the air only while she is falling (yVelocity <= 0)
 *   and not more than half a block below the bomb;
 * - the bounce sets yVelocity to SAMUS_BOMB_BOUNCE_Y_VELOCITY, which gravity then eats.
 * So where Samus will be, and which bombs will catch her, can be worked out in advance: each frame
 * a bomb can be laid, the next kLook frames are tried as the moment to lay it, and it is laid now
 * when now is the moment whose throw starts highest. tools/auto-bomb-sim.py (host) is the model's
 * twin used to pick the numbers. */

/* The option (mzm3ds.ini `auto_bomb_jump`, OPTIONS -> CONTROLS). On by default; off in RA hardcore. */
bool Port_AutoBomb_Enabled(void);
void Port_AutoBomb_SetEnabled(bool on);

/* Called once per game frame from the key poll, right before the frame runs. `bombHeld`: a button
 * that lays bombs is held; `bInput`: KEY_B as the player's buttons give it this frame. Returns
 * true when the assist takes over KEY_B this frame, with *press the value to send; false leaves
 * the input as it is. */
bool Port_AutoBomb_Translate(bool bombHeld, bool bInput, bool* press);

#endif
