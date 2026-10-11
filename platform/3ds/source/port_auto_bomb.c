#include "port_auto_bomb.h"

#include <stdint.h>
#include <stdlib.h>

#include "constants/game_state.h"
#include "constants/projectile.h"
#include "constants/samus.h"
#include "structs/projectile.h"
#include "structs/samus.h"
#include "types.h"

extern u8 gMainGameMode;
extern u8 gSubGameMode1;
extern u8 gCurrentArea;
extern u8 gCurrentRoom;
extern bool Port_RA_IsHardcore(void);

enum {
    kFuse = 33,          /* frames from the frame a bomb is laid in to the frame it looks for Samus */
    kLook = 16,          /* candidate moments to lay a bomb: the next kLook frames */
    kHorizon = kLook + kFuse + 4,
    kMaxBombs = PROJ_LIMIT_BOMB + 1,
    kCooldown = 7,       /* CONVERT_SECONDS(.1 + 1/60) */
    kBombTop = -60, kBombBottom = 48, /* the bomb's hitbox, sub pixels around its position */
    kSamusTop = -60,     /* the morph ball's: its bottom is the position itself */
    kHalfBlock = 32,
    kBounceVelocity = 164,
    kFloorUnknown = 0x7FFFFFFF,
};

typedef struct {
    int y, v;     /* sub pixels (grows downward) and yVelocity (positive up) */
    bool air;
} Body;

typedef struct {
    int y, at;    /* position when laid; frames from the next frame (s = 0) to the frame it looks */
} Bomb;

static bool sEnabled = true;
static int sHeld;             /* frames the bomb button has been held in a row */
static bool sSent;            /* B went to the game last frame */
static int sFloor = kFloorUnknown;   /* where Samus last rolled on the ground, this room */
static int sFloorArea = -1, sFloorRoom = -1;

bool Port_AutoBomb_Enabled(void) { return sEnabled; }
void Port_AutoBomb_SetEnabled(bool on) { sEnabled = on; }

static bool InMorphBall(void) {
    switch (gSamusData.pose) {
        case SPOSE_MORPH_BALL:
        case SPOSE_ROLLING:
        case SPOSE_MORPH_BALL_MIDAIR:
            return true;
        default:
            return false;
    }
}

/* One frame of the ball in the air, as SamusUpdateVelocityPosition moves it. */
static void Step(Body* b, int floor) {
    if (!b->air) return;
    int v = b->v;
    const int up = gSamusPhysics.positiveYVelocityCap, down = gSamusPhysics.negativeYVelocityCap;
    if (v > up) v = up;
    else if (v < -down) v = -down;
    if (b->v >= -231) b->v -= gSamusPhysics.yAcceleration;
    b->y -= v / 8;
    if (b->y >= floor && b->v <= 0) {
        b->y = floor;
        b->v = 0;
        b->air = false;
    }
}

/* Whether a bomb at `bombY` bounces the ball that is at `y` now (ProjectileCheckSamusBombBounce
 * and SamusBombBounce). */
static bool Bounces(const Body* b, int bombY) {
    const int top = b->y + kSamusTop, bottom = b->y;
    if (!(bottom > bombY + kBombTop && top < bombY + kBombBottom)) return false;
    if (!b->air) return true;
    return b->v <= 0 && !(b->y - kHalfBlock > bombY);
}

/* Plays `bombs` forward from Samus as she is now. Returns the height (sub pixels, lower is higher)
 * of the throw bomb `which` gives, or INT_MAX if it gives none. `path`, when given, gets her
 * height for the first kLook frames. */
static int Simulate(const Body* start, int floor, const Bomb* bombs, int n, int which, int* path) {
    Body b = *start;
    int result = INT32_MAX;
    for (int s = 0; s < kHorizon; s++) {
        Step(&b, floor);
        if (path && s < kLook) path[s] = b.y;
        for (int i = 0; i < n; i++) {
            if (bombs[i].at != s || !Bounces(&b, bombs[i].y)) continue;
            if (i == which) result = b.y;
            b.v = kBounceVelocity;
            b.air = true;
        }
    }
    return result;
}

static bool WantBomb(const Body* now, int floor) {
    Bomb bombs[kMaxBombs];
    int n = 0;
    for (int i = 0; i < MAX_AMOUNT_OF_PROJECTILES && n < kMaxBombs - 1; i++) {
        const struct ProjectileData* p = &gProjectileData[i];
        if (!(p->status & PROJ_STATUS_EXISTS) || p->type != PROJ_TYPE_BOMB) continue;
        int at;
        switch (p->movementStage) {
            case BOMB_STAGE_FIRST_SPIN: at = p->timer + 16; break;
            case BOMB_STAGE_FAST_SPIN: at = p->timer; break;
            case BOMB_STAGE_EXPLODING: at = p->timer == BOMB_EXPLOSION_TIMER ? 0 : -1; break;
            default: at = -1; break;
        }
        if (at >= 0) bombs[n++] = (Bomb){ p->yPosition, at };
    }
    int path[kLook];
    Simulate(now, floor, bombs, n, -1, path);
    int best = -1, bestY = 0;
    for (int k = 0; k < kLook; k++) {
        /* A throw only counts when it still lands one frame early or late: the model is not
         * frame exact about the order of the game's updates. */
        int worst = INT32_MIN;
        for (int d = -1; d <= 1; d++) {
            bombs[n] = (Bomb){ path[k], k + kFuse + d };
            const int y = Simulate(now, floor, bombs, n + 1, n, NULL);
            if (y == INT32_MAX) {
                worst = INT32_MAX;
                break;
            }
            if (y > worst) worst = y;
        }
        if (worst != INT32_MAX && (best < 0 || worst < bestY)) {
            best = k;
            bestY = worst;
        }
    }
    return best == 0;
}

static int BombCount(void) {
    int count = 0;
    for (int i = 0; i < MAX_AMOUNT_OF_PROJECTILES; i++) {
        const struct ProjectileData* p = &gProjectileData[i];
        if ((p->status & PROJ_STATUS_EXISTS) && p->type == PROJ_TYPE_BOMB) count++;
    }
    return count;
}

#ifdef PORT_DEBUG_TOOLS
/* The last frames of the assist, for tuning it from the PC: `mzm-link.py peek gAutoBombTrace 3072`
 * (one entry per key poll, oldest at gAutoBombTraceNext). Debug builds only. */
struct AutoBombTraceEntry {
    int16_t y;        /* Samus's yPosition minus where the ball rolled on the ground, sub pixels */
    int16_t v;        /* yVelocity */
    uint16_t samusX;
    uint8_t pose;
    uint8_t flags;    /* bit 0 B sent, bit 1 the assist is in control, bit 2 a bomb was laid, bits 4.. bombs alive */
};
struct AutoBombTraceEntry gAutoBombTrace[512];
uint32_t gAutoBombTraceNext;
static void Trace(bool sent, bool control, bool laid) {
    struct AutoBombTraceEntry* e = &gAutoBombTrace[gAutoBombTraceNext++ % 512];
    e->y = (int16_t)((sFloor == kFloorUnknown ? 0 : sFloor) - (int)gSamusData.yPosition);
    e->v = (int16_t)gSamusData.yVelocity;
    e->samusX = gSamusData.xPosition;
    e->pose = gSamusData.pose;
    e->flags = (uint8_t)((sent ? 1 : 0) | (control ? 2 : 0) | (laid ? 4 : 0) | (BombCount() << 4));
}
#define TRACE(sent, control, laid) Trace((sent), (control), (laid))
#else
#define TRACE(sent, control, laid) ((void)0)
#endif

bool Port_AutoBomb_Translate(bool bombHeld, bool bInput, bool* press) {
    sHeld = bombHeld ? sHeld + 1 : 0;

    /* Where the ground is, for the throws that come back down: where she last rolled, this room. */
    if (gCurrentArea != sFloorArea || gCurrentRoom != sFloorRoom) {
        sFloorArea = gCurrentArea;
        sFloorRoom = gCurrentRoom;
        sFloor = kFloorUnknown;
    }
    if ((gSamusData.pose == SPOSE_MORPH_BALL || gSamusData.pose == SPOSE_ROLLING) &&
        gSamusData.standingStatus == STANDING_GROUND)
        sFloor = gSamusData.yPosition;

    /* The first frame of a press is the player's own bomb; from the next one on, while the button is
     * held, the bombs are ours. */
    if (!sEnabled || sHeld < 2 || Port_RA_IsHardcore() || gMainGameMode != GM_INGAME ||
        gSubGameMode1 != SUB_GAME_MODE_PLAYING || gPreventMovementTimer != 0 || !InMorphBall() ||
        !(gEquipment.beamBombsActivation & BBF_BOMBS) || (gSamusWeaponInfo.weaponHighlighted & WH_POWER_BOMB) ||
        gSamusPhysics.slowedByLiquid) {
        sSent = bInput;
        TRACE(bInput, false, false);
        return false;
    }
    /* A bomb needs a fresh press (a frame with the button down, then one with it up), the weapon's
     * cooldown over and fewer than the limit laid. */
    bool lay = false;
    if (!sSent && gSamusWeaponInfo.cooldown == 0 && BombCount() < PROJ_LIMIT_BOMB) {
        Body now = { gSamusData.yPosition, gSamusData.yVelocity, gSamusData.pose == SPOSE_MORPH_BALL_MIDAIR };
        lay = WantBomb(&now, sFloor);
    }
    sSent = lay;
    *press = lay;
    TRACE(lay, true, lay);
    return true;
}
