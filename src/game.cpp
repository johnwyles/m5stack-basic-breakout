#include "game.h"
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "entities.h"
#include "levels.h"
#include "storage.h"
#include "audio.h"
#include "input.h"
#include "hud.h"
#include "menu.h"
#include "gfx.h"
#include "fx.h"
#include "pill.h"

Settings g_settings = { DIFF_NORMAL, true };

// The canvas moved into gfx.cpp so the menu can share it for attract mode.
// Everything below draws into gfx_canvas() and pushes once per frame.

struct GameCtx {
    GameState state;
    bool      screenDrawn;        // one-shot screens: already painted?
    uint32_t  stateMs;            // millis() when the state was entered

    uint8_t   level;
    uint32_t  score;
    uint32_t  highScore;
    bool      beatHigh;
    uint8_t   lives;

    uint8_t   bricks[BRICK_COUNT];
    uint16_t  bricksLeft;

    Ball      balls[MAX_BALLS];
    Paddle    paddle;
    Drop      drops[MAX_DROPS];

    float     baseSpeed;          // difficulty * level scaling
    float     speedMul;           // slow/fast powerups
    float     paddleVel;          // MEASURED px/tick, after clamping
    uint16_t  stickyTicks;
    uint16_t  bigTicks;           // BIG BALL
    uint16_t  explosiveTicks;     // every brick the ball touches detonates
    uint16_t  laserTicks;
    uint16_t  holeTicks;
    uint16_t  throughTicks;
    uint16_t  speedTicks;         // SLOW / FAST are timed now
    uint16_t  barrierTicks;
    int16_t   barrierCount;
    int16_t   barrierCells[BRICK_COLS];
    uint16_t  fogTicks;
    uint16_t  boltCooldown;

    uint32_t  respawnAt;          // 0 == not waiting
    uint32_t  toastUntil;
    int8_t    serveLean;          // alternates -1 / +1 between serves

    int16_t   ballSize;           // global, not per-ball: BIG BALL grows all of them
    uint16_t  doubleTicks;        // 2X score
    uint8_t   bestChain;          // longest rally this game, shown on game over
    float     gridDrop;           // px the whole brick grid has descended
    float     gridDropTarget;     // where it is easing to
    bool      crushed;            // grid reached the paddle
    bool      netActive;          // one-shot floor across the bottom

    uint16_t  introTicks;         // level intro: rows dropping in, physics held
    uint16_t  outroTicks;         // level clear / game over: grid shattering
    uint8_t   paddleFlash;
    uint32_t  frameTick;          // free-running, drives drop wobble
    bool      keyFromPause;       // where to return when the key screen closes
};

static GameCtx g;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline bool overlaps(float ax, float ay, float aw, float ah,
                            float bx, float by, float bw, float bh) {
    return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

static void setState(GameState st) {
    g.state       = st;
    g.screenDrawn = false;
    g.stateMs     = millis();
}

static float currentSpeed() {
    return clampf(g.baseSpeed * g.speedMul, 0.5f, BALL_SPEED_CAP);
}

// ---------------------------------------------------------------------------
// Changes the speed multiplier AND rescales every ball already in flight.
//
// The rescale is the whole point. currentSpeed() is only consulted when a ball
// bounces off the paddle, so setting the multiplier on its own did nothing
// until the next paddle contact. Catching SLOW felt delayed, and far worse, the
// timer expiring and restoring 1.0 did not speed the ball back up until it came
// down again. During a long rally up in the bricks that is many seconds, which
// is exactly why SLOW looked like it never timed out.
// ---------------------------------------------------------------------------
static void setSpeedMul(float mul) {
    mul = clampf(mul, SPEED_MUL_MIN, SPEED_MUL_MAX);
    if (g.speedMul <= 0.01f) { g.speedMul = mul; return; }

    float k = mul / g.speedMul;
    g.speedMul = mul;

    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        Ball& b = g.balls[i];
        if (!b.alive || b.stuck) continue;

        b.vx *= k;
        b.vy *= k;

        // Never let a scale-up push a ball past the tunnelling cap.
        float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (sp > BALL_SPEED_CAP && sp > 0.01f) {
            float n = BALL_SPEED_CAP / sp;
            b.vx *= n;
            b.vy *= n;
        }
    }
}

static inline bool explosiveActive() { return g.explosiveTicks > 0; }

// ---------------------------------------------------------------------------
// Magnus: curve the flight without changing its speed.
//
// The renormalise is not optional. Spin must alter DIRECTION only; if it added
// energy the ball would creep past BALL_SPEED_CAP and start tunnelling through
// bricks between frames, quietly breaking a guarantee the whole collision
// system rests on.
// ---------------------------------------------------------------------------
static void applySpin(Ball& b) {
    if (b.stuck) return;

    if (fabsf(b.spin) <= SPIN_MIN) { b.spin = 0.0f; return; }

    float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
    if (sp > 0.01f) {
        float px = -b.vy / sp;          // perpendicular to travel, normalised
        float py =  b.vx / sp;

        b.vx += px * SPIN_MAGNUS * b.spin;
        b.vy += py * SPIN_MAGNUS * b.spin;

        float ns = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (ns > 0.01f) { b.vx *= sp / ns; b.vy *= sp / ns; }
    }
    b.spin *= SPIN_DECAY;
}

static void clearBarrier();   // defined with applyBarrier, used by the reset paths

// How far above base speed the balls are running, 0..1. FAST steps this up and
// SLOW steps it back down, so the flame is a readout of the current speed
// rather than of a pill, and bringing the speed back down visibly cools it.
// Heat is measured from a ball's ACTUAL speed against the level's base speed,
// not from the speed multiplier. Two reasons. THROUGH lights the ball without
// touching speed at all, so a multiplier readout would miss it entirely. And
// gating the flame on a pill you might never catch meant it could go a whole
// run without appearing, which is exactly what happened: SLOW pushed speedMul
// below 1.0 and nothing ever reset it, so the trail could not light again for
// the rest of the game.
static float fireHeat(const Ball& b) {
    if (g.throughTicks > 0) return 1.0f;          // a THROUGH ball always burns
    if (g.baseSpeed <= 0.01f) return 0.0f;

    float sp    = sqrtf(b.vx * b.vx + b.vy * b.vy);
    float ratio = sp / g.baseSpeed;
    if (ratio <= FIRE_THRESHOLD) return 0.0f;

    float h = (ratio - FIRE_THRESHOLD) / (SPEED_MUL_MAX - FIRE_THRESHOLD);
    return (h > 1.0f) ? 1.0f : h;
}


// The grid's current top edge, including anything DESCEND has added. Drawing
// and collision BOTH go through this. If they each applied the offset
// themselves they would eventually disagree, and a ball bouncing off bricks
// that are not where they are drawn is an unfindable bug.
static inline int16_t gridTopY() {
    return GRID_Y0 + (int16_t)g.gridDrop;
}

static void brickTopLeft(int16_t c, int16_t r, int16_t& bx, int16_t& by) {
    bx = GRID_X0 + c * BRICK_PITCH_X;
    by = gridTopY() + r * BRICK_PITCH_Y;
}

// Bottom edge of the lowest row still holding anything, or -1 for an empty
// grid. Solid bricks count: being crushed by something indestructible is
// exactly as fatal.
static int16_t lowestBrickBottom() {
    for (int16_t r = BRICK_ROWS - 1; r >= 0; --r) {
        for (int16_t c = 0; c < BRICK_COLS; ++c) {
            if (g.bricks[r * BRICK_COLS + c] != BRICK_EMPTY) {
                return gridTopY() + r * BRICK_PITCH_Y + BRICK_H;
            }
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// All scoring funnels through here so the 2X multiplier has exactly one place
// to apply. Every `g.score +=` outside this function is a bug waiting to
// happen the next time a scoring source is added.
// ---------------------------------------------------------------------------
static uint32_t addScore(uint32_t pts, uint8_t chain = 1) {
    // Integer throughout: the multiplier is applied to the base value rather
    // than compounded, so a long chain cannot accumulate rounding drift into
    // the score the player is trying to beat.
    uint16_t steps = (chain > 1) ? (uint16_t)(chain - 1) : 0;
    if (steps > COMBO_MAX_STEPS) steps = COMBO_MAX_STEPS;

    uint32_t award = pts * (100 + COMBO_BONUS_PCT * steps) / 100;
    if (g.doubleTicks > 0) award *= 2;             // stacks, deliberately

    g.score += award;
    return award;
}

// Chain heat: the popup and the ball's trail brighten as the run grows, so the
// state of the combo is visible continuously rather than only at payout.
static uint16_t comboColor(uint8_t chain, uint16_t base) {
    if (g.doubleTicks > 0) return DROPS[PU_DOUBLE].color;
    if (chain < COMBO_POPUP_MIN) return base;

    uint16_t steps = (uint16_t)(chain - 1);
    if (steps > COMBO_MAX_STEPS) steps = COMBO_MAX_STEPS;
    return lighten565(base, (uint8_t)(COMBO_TINT_MAX * steps / COMBO_MAX_STEPS));
}

static void scorePopup(float x, float y, uint32_t award, uint8_t chain, uint16_t base) {
    char b[12];
    if (chain >= COMBO_POPUP_MIN) {
        snprintf(b, sizeof(b), "+%lu x%u", (unsigned long)award, (unsigned)chain);
    } else {
        snprintf(b, sizeof(b), "+%lu", (unsigned long)award);
    }
    fx_popup(x, y, b, comboColor(chain, base));
}

static inline uint16_t scoreColor(uint16_t fallback) {
    return (g.doubleTicks > 0) ? DROPS[PU_DOUBLE].color : fallback;
}

// ---------------------------------------------------------------------------
// Brick damage flash
//
// A multi-hit brick used to signal damage only by gaining a thin line, which is
// easy to miss at speed. A two-frame white overlay makes "that hit landed but
// did not break it" unmistakable. Several slots because multiball can land on
// more than one brick in the same tick.
// ---------------------------------------------------------------------------
struct BrickFlash { int16_t idx; uint8_t ticks; };
static BrickFlash s_bflash[BRICK_FLASH_SLOTS];

static void clearBrickFlash() {
    for (int16_t i = 0; i < BRICK_FLASH_SLOTS; ++i) s_bflash[i].ticks = 0;
}

static void flashBrick(int16_t idx) {
    for (int16_t i = 0; i < BRICK_FLASH_SLOTS; ++i) {
        if (s_bflash[i].ticks && s_bflash[i].idx == idx) {
            s_bflash[i].ticks = BRICK_FLASH_TICKS;
            return;
        }
    }
    for (int16_t i = 0; i < BRICK_FLASH_SLOTS; ++i) {
        if (!s_bflash[i].ticks) {
            s_bflash[i].idx   = idx;
            s_bflash[i].ticks = BRICK_FLASH_TICKS;
            return;
        }
    }
}

static bool brickFlashing(int16_t idx) {
    for (int16_t i = 0; i < BRICK_FLASH_SLOTS; ++i) {
        if (s_bflash[i].ticks && s_bflash[i].idx == idx) return true;
    }
    return false;
}

static void tickBrickFlash() {
    for (int16_t i = 0; i < BRICK_FLASH_SLOTS; ++i) {
        if (s_bflash[i].ticks) --s_bflash[i].ticks;
    }
}

// ---------------------------------------------------------------------------
// Resizes every ball on screen, growing or shrinking about each one's centre.
//
// Global rather than per-ball by design: catching BIG BALL with three balls in
// play grows all three, and a ball spawned by MULTIBALL afterwards inherits the
// current size because it reads the same field.
//
// Growth can leave a ball overlapping a brick it was resting against by up to
// one pixel per side. That is left for resolveBricks() to sort out on the next
// tick, which pushes it flush and damages the brick. Against a destructible
// brick that reads as the bigger ball immediately earning its keep; against a
// solid one it just pushes clear.
// ---------------------------------------------------------------------------
static bool setBallSize(int16_t want) {
    if (want < BALL_SIZE_MIN) want = BALL_SIZE_MIN;
    if (want > BALL_SIZE_MAX) want = BALL_SIZE_MAX;
    if (want == g.ballSize) return false;

    float shift = (want - g.ballSize) * 0.5f;
    g.ballSize = want;

    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        Ball& b = g.balls[i];
        if (!b.alive) continue;

        b.x = clampf(b.x - shift, 0.0f, (float)(PLAY_W - g.ballSize));
        b.y = clampf(b.y - shift, 0.0f, (float)(PLAY_H - g.ballSize));
        b.trailCount = 0;                          // old trail is the wrong size
    }
    return true;
}

// ---------------------------------------------------------------------------
// Laser bolts
//
// Fired manually on a short BtnB press. Auto-fire would have dodged the control
// conflict, but tapping to shoot is the thing that makes a paddle gun feel like
// a gun, so pause moves to a long press for the duration instead.
//
// Bolts do NOT feed the rally combo. That counter measures what a ball has done
// since it last touched the paddle, and a bolt is not a ball.
// ---------------------------------------------------------------------------
struct Bolt { bool active; float x, y; };
static Bolt s_bolts[MAX_BOLTS];

static void clearBolts() {
    for (int16_t i = 0; i < MAX_BOLTS; ++i) s_bolts[i].active = false;
}

static void fireBolt(float x) {
    for (int16_t i = 0; i < MAX_BOLTS; ++i) {
        if (s_bolts[i].active) continue;
        s_bolts[i].active = true;
        s_bolts[i].x = x;
        s_bolts[i].y = (float)(PADDLE_Y - BOLT_H);
        return;
    }
}

// ---------------------------------------------------------------------------
// Powerup drops
// ---------------------------------------------------------------------------
static DropType pickDropType() {
    // Negative weights grow with the level. One line, and it turns a flat drop
    // table into a difficulty curve: level 1 sits near a third hostile, level
    // 10 near a half, without any of the weights above changing.
    float negScale = 1.0f + NEG_SCALE_PER_LEVEL * (float)(g.level - 1);
    if (negScale > NEG_SCALE_MAX) negScale = NEG_SCALE_MAX;

    uint32_t total = 0;
    for (uint8_t i = 0; i < PU_COUNT; ++i) {
        total += DROPS[i].negative ? (uint32_t)(DROPS[i].weight * negScale)
                                   : DROPS[i].weight;
    }
    if (total == 0) return PU_GROW;

    uint32_t roll = (uint32_t)random((long)total);
    for (uint8_t i = 0; i < PU_COUNT; ++i) {
        uint32_t w = DROPS[i].negative ? (uint32_t)(DROPS[i].weight * negScale)
                                       : DROPS[i].weight;
        if (roll < w) return (DropType)i;
        roll -= w;
    }
    return PU_GROW;
}

static void spawnDrop(int16_t bx, int16_t by) {
    for (int16_t i = 0; i < MAX_DROPS; ++i) {
        if (g.drops[i].active) continue;
        g.drops[i].active = true;
        g.drops[i].type   = pickDropType();
        g.drops[i].x      = bx + (BRICK_W - DROP_W) * 0.5f;
        g.drops[i].y      = (float)by;
        g.drops[i].phase  = (uint8_t)random(256);
        return;
    }
    // All slots busy: the drop is simply skipped rather than queued.
}

static void clearDrops() {
    for (int16_t i = 0; i < MAX_DROPS; ++i) g.drops[i].active = false;
}

// ---------------------------------------------------------------------------
// Ball helpers
// ---------------------------------------------------------------------------
static uint8_t aliveBalls() {
    uint8_t n = 0;
    for (int16_t i = 0; i < MAX_BALLS; ++i) if (g.balls[i].alive) ++n;
    return n;
}

static void rotateVel(Ball& b, float radians) {
    float c = cosf(radians), s = sinf(radians);
    float nx = b.vx * c - b.vy * s;
    float ny = b.vx * s + b.vy * c;
    b.vx = nx;
    b.vy = ny;
}

static void serveBall() {
    for (int16_t i = 0; i < MAX_BALLS; ++i) g.balls[i].alive = false;

    Ball& b = g.balls[0];
    b.alive       = true;
    b.stuck       = true;
    b.stickOffset = 0.0f;
    b.vx = b.vy   = 0.0f;
    b.trailHead   = 0;
    b.trailCount  = 0;
    b.combo       = 0;
    b.spin        = 0.0f;
    b.x = g.paddle.x + g.paddle.w * 0.5f - g.ballSize * 0.5f;
    b.y = PADDLE_Y - g.ballSize;

    // Temporary effects do not survive a death; paddle size and speed do.
    g.explosiveTicks = g.laserTicks = 0;
    g.holeTicks = g.fogTicks = g.throughTicks = 0;
    g.speedTicks = 0;
    g.speedMul = 1.0f;
    clearBarrier();
    if (g.bigTicks > 0) { g.bigTicks = 0; setBallSize(BALL_SIZE_BASE); }
    clearBolts();
    clearDrops();
}

static void launchStuckBalls() {
    bool launched = false;
    float speed = currentSpeed();

    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        Ball& b = g.balls[i];
        if (!b.alive || !b.stuck) continue;

        // Alternate the lean so the opening shot is not perfectly predictable.
        float ang = g.serveLean * (PI / 9.0f);          // +/- 20 degrees
        b.vx   = speed * sinf(ang);
        b.vy   = -speed * cosf(ang);
        b.stuck = false;
        launched = true;
    }
    if (launched) {
        g.serveLean = (int8_t)-g.serveLean;
        sfx_launch();
    }
}

static void doMultiBall() {
    int16_t src = -1;
    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        if (g.balls[i].alive) { src = i; break; }
    }
    if (src < 0) return;

    float speed = currentSpeed();
    uint8_t spawned = 0;

    for (int16_t j = 0; j < MAX_BALLS && spawned < 2; ++j) {
        if (g.balls[j].alive) continue;

        g.balls[j] = g.balls[src];
        g.balls[j].alive      = true;
        g.balls[j].stuck      = false;
        g.balls[j].trailCount = 0;   // a fresh ball has no history to draw
        g.balls[j].trailHead  = 0;

        if (g.balls[src].stuck) {
            // Splitting a held ball: give the clones a fresh upward vector.
            float ang = (spawned == 0 ? -1.0f : 1.0f) * (PI / 5.0f);
            g.balls[j].vx = speed * sinf(ang);
            g.balls[j].vy = -speed * cosf(ang);
        } else {
            rotateVel(g.balls[j], (spawned == 0 ? -0.44f : 0.44f));
        }
        ++spawned;
    }
}

// ---------------------------------------------------------------------------
// Bricks
// ---------------------------------------------------------------------------
static void destroyBrick(int16_t idx, bool allowDrop, const Ball* hitter = nullptr) {
    int16_t row = idx / BRICK_COLS;
    int16_t col = idx % BRICK_COLS;

    int16_t bx, by;
    brickTopLeft(col, row, bx, by);
    float cx = bx + BRICK_W * 0.5f;
    float cy = by + BRICK_H * 0.5f;

    uint8_t chain = hitter ? hitter->combo : 1;

    g.bricks[idx] = BRICK_EMPTY;
    uint32_t award = addScore(ROW_POINTS[row], chain);

    fx_burst(cx, cy, 5, ROW_COLORS[row], 1.7f);
    scorePopup(cx, cy, award, chain, ROW_COLORS[row]);

    if (allowDrop && (uint8_t)random(100) < DROP_CHANCE) {
        spawnDrop(bx, by);
    }
}

static void explodeBrick(int16_t idx, const Ball* hitter = nullptr) {
    int16_t row = idx / BRICK_COLS;
    int16_t col = idx % BRICK_COLS;

    int16_t ex, ey;
    brickTopLeft(col, row, ex, ey);

    destroyBrick(idx, true, hitter);
    sfx_explode();

    fx_burst(ex + BRICK_W * 0.5f, ey + BRICK_H * 0.5f, 16, COL_EXPLOSIVE, 3.0f);
    fx_shake(4, 14);

    for (int16_t dr = -1; dr <= 1; ++dr) {
        for (int16_t dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int16_t r = row + dr, c = col + dc;
            if (r < 0 || r >= BRICK_ROWS || c < 0 || c >= BRICK_COLS) continue;

            int16_t n = r * BRICK_COLS + c;
            uint8_t cell = g.bricks[n];
            if (cell == BRICK_EMPTY || cell == BRICK_SOLID) continue;

            if (cell == BRICK_EXPLOSIVE) {
                // No chain reaction: just clear it, or a full grid of these
                // would recurse straight into a stack overflow.
                int16_t nx, ny;
                brickTopLeft(c, r, nx, ny);
                // Blast victims are paid at the chain the ball had earned, but
                // they do not ADVANCE it. Letting them would mean one bomb
                // jumping the multiplier most of the way to its cap, which
                // rewards the pickup rather than the rally.
                g.bricks[n] = BRICK_EMPTY;
                addScore(ROW_POINTS[r], hitter ? hitter->combo : 1);
                fx_burst(nx + BRICK_W * 0.5f, ny + BRICK_H * 0.5f, 6, COL_EXPLOSIVE, 2.2f);
            } else {
                destroyBrick(n, false, hitter);
            }
        }
    }
}

// Applies damage. Returns true if the brick is indestructible.
static void damageBrick(int16_t idx, Ball* hitter = nullptr) {
    uint8_t cell = g.bricks[idx];

    if (cell == BRICK_SOLID) {
        // Solid bricks deliberately do NOT advance the chain. They are an
        // infinite source of contacts, so a ball wedged between two of them
        // would farm the multiplier to its cap and hold it there forever.
        sfx_brick_solid();
        flashBrick(idx);
        return;
    }

    // Any contact with a destructible brick counts, including a hit that only
    // damages an armoured one. The chain measures work done on the grid, not
    // kills.
    if (hitter && hitter->combo < 250) {
        ++hitter->combo;
        if (hitter->combo > g.bestChain) g.bestChain = hitter->combo;
    }

    // A primed bomb turns the next contact into a detonation regardless of how
    // much health the brick had left. Solid bricks do not consume it: spending
    // your bomb on something indestructible would feel like a robbery.
    // EXPLOSIVE BALL: every brick the ball touches detonates for as long as it
    // lasts. This replaced the one-shot BOMB pill; two pills doing the same
    // thing at different durations is exactly what makes a key screen useless.
    if (explosiveActive()) {
        explodeBrick(idx, hitter);
        g.bricksLeft = levels_destructible_remaining(g.bricks);
        return;
    }

    if (cell == BRICK_EXPLOSIVE) {
        explodeBrick(idx, hitter);
    } else if (cell > BRICK_HP1) {
        g.bricks[idx] = cell - 1;      // damaged, no points awarded
        sfx_brick_hit();
        flashBrick(idx);
    } else {
        destroyBrick(idx, true, hitter);
        sfx_brick_break();
    }
}

// Resolves brick collisions on one axis. Movement is axis-separated (move x,
// resolve, then move y, resolve), which handles corner hits correctly without
// needing a penetration-depth comparison.
// Does this brick stop the ball, as opposed to being ploughed through?
static bool brickBlocks(uint8_t cell) {
    if (!(g.throughTicks > 0)) return true;
    if (cell == BRICK_SOLID || cell == BRICK_EXPLOSIVE) return true;
    return cell > THROUGH_MAX_HP;     // 2HP and up still stop a THROUGH ball
}

// ---------------------------------------------------------------------------
// Brick collision.
//
// This used to stop at the FIRST overlapping brick, push the ball flush against
// it and return. That is why ball size was capped by the brick gap: a ball
// wider than the gap got pushed out of one brick and straight into its
// neighbour, and the two fought forever. Growing the ball meant widening the
// gaps, which made the grid look like a picket fence for a ball that was still
// small.
//
// Now it gathers every overlapping brick, damages all of them, and resolves
// position against the DEEPEST penetration on the axis that moved. The gap
// stops mattering entirely, and a wide ball smashing three bricks in one
// contact becomes the point rather than a bug to design around.
// ---------------------------------------------------------------------------
static void resolveBricks(Ball& b, bool horizontal) {
    // Ball coordinates are floats, so the scan range must cover the ball's full
    // extent (x .. x+g.ballSize), not x+g.ballSize-1. A "-1" here is
    // integer-pixel thinking and silently drops sub-pixel overlaps, which is
    // exactly how a ball ends up embedded in a brick it was never tested
    // against. Including one cell too many is harmless: overlaps() is
    // authoritative.
    int16_t c0 = (int16_t)floorf((b.x - GRID_X0) / (float)BRICK_PITCH_X);
    int16_t c1 = (int16_t)floorf((b.x + g.ballSize - GRID_X0) / (float)BRICK_PITCH_X);
    int16_t r0 = (int16_t)floorf((b.y - gridTopY()) / (float)BRICK_PITCH_Y);
    int16_t r1 = (int16_t)floorf((b.y + g.ballSize - gridTopY()) / (float)BRICK_PITCH_Y);

    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > BRICK_COLS - 1) c1 = BRICK_COLS - 1;
    if (r1 > BRICK_ROWS - 1) r1 = BRICK_ROWS - 1;
    if (c1 < c0 || r1 < r0) return;

    bool    blocked   = false;
    bool    allSolid  = true;    // every blocker was indestructible
    float   bestEdge  = 0.0f;    // where to park the ball
    float   bestDepth = -1.0f;

    for (int16_t r = r0; r <= r1; ++r) {
        for (int16_t c = c0; c <= c1; ++c) {
            int16_t idx  = r * BRICK_COLS + c;
            uint8_t cell = g.bricks[idx];
            if (cell == BRICK_EMPTY) continue;

            int16_t bx, by;
            brickTopLeft(c, r, bx, by);
            if (!overlaps(b.x, b.y, g.ballSize, g.ballSize, bx, by, BRICK_W, BRICK_H)) continue;

            bool blocks = brickBlocks(cell);

            if (blocks) {
                // Depth of penetration along the axis we just moved on. The
                // deepest overlap is the one that actually stopped the ball;
                // shallower ones are grazes and must not dictate the rebound.
                float depth, edge;
                if (horizontal) {
                    if (b.vx > 0) { depth = (b.x + g.ballSize) - bx;      edge = (float)(bx - g.ballSize); }
                    else          { depth = (bx + BRICK_W) - b.x;         edge = (float)(bx + BRICK_W);    }
                } else {
                    if (b.vy > 0) { depth = (b.y + g.ballSize) - by;      edge = (float)(by - g.ballSize); }
                    else          { depth = (by + BRICK_H) - b.y;         edge = (float)(by + BRICK_H);    }
                }
                if (depth > bestDepth) { bestDepth = depth; bestEdge = edge; }

                blocked = true;
                if (cell != BRICK_SOLID) allSolid = false;
            }

            // Damaged regardless of whether it blocked: a THROUGH ball destroys
            // the 1HP bricks it passes through, and a wide ball takes out every
            // brick it is touching, not just the one it bounced off.
            damageBrick(idx, &b);
        }
    }

    if (!blocked) return;

    if (horizontal) {
        // A ball wider than the gap cannot be parked flush between two solid
        // bricks, because no such position exists. Backing it out along its own
        // velocity is the only honest answer; hunting for an edge would wedge
        // it. Destructible blockers are fine, they are gone next tick anyway.
        if (allSolid && g.ballSize > (BRICK_PITCH_X - BRICK_W)) b.x -= b.vx;
        else                                                    b.x  = bestEdge;
        b.vx = -b.vx;
    } else {
        if (allSolid && g.ballSize > (BRICK_PITCH_Y - BRICK_H)) b.y -= b.vy;
        else                                                    b.y  = bestEdge;
        b.vy = -b.vy;
    }

    // Contact reverses the sense of the spin and damps it, which also stops a
    // ball settling into a permanent orbit.
    b.spin *= SPIN_BOUNCE_RETAIN;
}

// ---------------------------------------------------------------------------
// Paddle
// ---------------------------------------------------------------------------
static void setPaddleWidth(int16_t w) {
    float cx = g.paddle.x + g.paddle.w * 0.5f;
    g.paddle.w = (int16_t)clampf((float)w, PADDLE_W_MIN, PADDLE_W_MAX);
    g.paddle.x = clampf(cx - g.paddle.w * 0.5f, 0.0f, (float)(PLAY_W - g.paddle.w));
}

static void updatePaddle() {
    bool left  = btnA_held();
    bool right = btnC_held();

    int8_t dir = 0;
    if (left && !right)      dir = -1;
    else if (right && !left) dir = +1;

    // Speed ramps while held so taps are precise and holds cover ground.
    if (dir == 0 || dir != g.paddle.dir) {
        g.paddle.speed = PADDLE_SPEED_MIN;
    } else {
        g.paddle.speed = clampf(g.paddle.speed + PADDLE_ACCEL,
                                PADDLE_SPEED_MIN, PADDLE_SPEED_MAX);
    }
    g.paddle.dir = dir;

    // Measured, not intended. Holding left while already pinned against the
    // wall means the paddle is NOT moving and must impart no spin; using
    // dir * speed here would give phantom English off both walls, in exactly
    // the situation the player is least able to explain.
    float prev = g.paddle.x;
    g.paddle.x = clampf(g.paddle.x + dir * g.paddle.speed,
                        0.0f, (float)(PLAY_W - g.paddle.w));
    g.paddleVel = g.paddle.x - prev;
}

static void ballVsPaddle(Ball& b) {
    if (b.vy <= 0) return;

    // Captured BEFORE anything overwrites it. The outgoing vx is computed from
    // the impact position further down, and slip was being measured against
    // that instead of against the ball's real incoming motion. It made a
    // centre hit produce exactly zero slip by construction, because a centre
    // hit has an outgoing vx of zero, which removed the natural spin a still
    // paddle is supposed to impart and gutted the whole model.
    float inVx = b.vx;
    if (!overlaps(b.x, b.y, g.ballSize, g.ballSize,
                  g.paddle.x, (float)PADDLE_Y, (float)g.paddle.w, (float)PADDLE_H)) return;

    // HOLE: the paddle keeps its width, the middle just stops working. Checked
    // before anything else so a ball through the gap is untouched, not caught
    // and then released.
    if (g.holeTicks > 0) {
        float bc = b.x + g.ballSize * 0.5f;
        float pc = g.paddle.x + g.paddle.w * 0.5f;
        if (fabsf(bc - pc) < HOLE_W * 0.5f) return;
    }

    b.y = (float)(PADDLE_Y - g.ballSize);
    g.paddleFlash = PADDLE_FLASH_TICKS;

    // Returning to the paddle is what closes the rally, so the chain banks and
    // resets here. Walls do not reset it, and neither does the safety net: the
    // net is a rescue, not a rally.
    if (b.combo >= COMBO_CHEER_MIN) {
        char msg[12];
        snprintf(msg, sizeof(msg), "CHAIN %u", (unsigned)b.combo);
        fx_popup(g.paddle.x + g.paddle.w * 0.5f, (float)(PADDLE_Y - 26),
                 msg, comboColor(b.combo, COL_TEXT));
    }
    b.combo = 0;

    if (g.stickyTicks > 0) {
        b.stuck       = true;
        b.stickOffset = (b.x + g.ballSize * 0.5f) - (g.paddle.x + g.paddle.w * 0.5f);
        b.vx = b.vy   = 0.0f;
        b.spin        = 0.0f;
        sfx_paddle();
        return;
    }

    // Impact offset from centre sets the outgoing angle, and the magnitude is
    // renormalised every time so long rallies never drift faster or slower.
    float half = g.paddle.w * 0.5f;
    float t    = clampf(((b.x + g.ballSize * 0.5f) - (g.paddle.x + half)) / half, -1.0f, 1.0f);
    float ang  = t * (PI / 3.0f);            // up to +/- 60 degrees from position
    float sp   = currentSpeed();

    // Spin carried INTO the hit bends the departure angle. This is what makes
    // spin worth setting up: you load the ball on one bounce and cash it in on
    // the next, rather than spin being something that merely happens to you.
    ang += clampf(b.spin, -SPIN_MAX, SPIN_MAX) * SPIN_CARRY;
    ang  = clampf(ang, -PADDLE_ANG_MAX, PADDLE_ANG_MAX);

    // Most of the charge is spent turning the ball. Without this a loaded ball
    // would keep its spin through every bounce forever.
    b.spin *= SPIN_CARRY_CONSUME;

    b.vx = sp * sinf(ang);

    // Tangential slip at the contact point, from the ball's INCOMING motion.
    // A paddle chasing the ball at matching speed leaves near-zero slip and
    // bounces clean; a paddle driven INTO the direction the ball came from
    // leaves large slip and bites hard; a still paddle leaves the ball's own
    // incoming vx, which is the mild natural amount.
    float slip = inVx - g.paddleVel;

    // Friction drags the ball toward the paddle's direction of travel, and the
    // same slip loads the spin that will curve the flight.
    // Both terms take the SAME sign off slip, and that sign is negative.
    //
    // This was the third and worst of the bugs. Friction kicked the ball toward
    // the paddle's direction of travel while the spin term, being positive,
    // curved it back the other way. The kick and the curve spent the whole
    // flight cancelling each other, which is precisely why the path looked
    // straight no matter how hard the constants were pushed. They now reinforce.
    b.vx  -= SPIN_FRICTION * slip;
    b.spin = clampf(b.spin - SPIN_GAIN * slip, -SPIN_MAX, SPIN_MAX);

    // Aiming by impact position stays the dominant skill, so friction is capped
    // well short of flattening the bounce. It also keeps vy large enough that
    // the anti-stall floor never has to fight it.
    b.vx = clampf(b.vx, -sp * SPIN_MAX_VX_FRAC, sp * SPIN_MAX_VX_FRAC);
    b.vy = -sqrtf(fmaxf(sp * sp - b.vx * b.vx, 0.04f));
    if (fabsf(b.vy) < BALL_VY_FLOOR) b.vy = -BALL_VY_FLOOR;

#if SPIN_DEBUG
    // One line per paddle hit. Two rounds of blind tuning is enough: this says
    // whether the spin is actually being generated, separately from whether it
    // is visible once it is.
    {
        float sp2  = sqrtf(b.vx * b.vx + b.vy * b.vy);
        float degT = (sp2 > 0.01f)
                   ? (SPIN_MAGNUS * fabsf(b.spin) / sp2) * 57.2958f : 0.0f;
        Serial.printf("[spin] inVx %+5.2f padVel %+5.2f slip %+5.2f -> spin %+5.2f "
                      "| outVx %+5.2f ang %+5.1fdeg | curve %.2f deg/tick\n",
                      inVx, g.paddleVel, slip, b.spin,
                      b.vx, ang * 57.2958f, degT);
    }
#endif

    sfx_paddle();
}

// ---------------------------------------------------------------------------
// ARMOR: every brick that was one hit from dying becomes two. Legible instantly
// now that bricks print their remaining hits, which is what makes this work as
// a punishment rather than as an invisible tax.
static void applyArmor() {
    for (int16_t i = 0; i < BRICK_COUNT; ++i) {
        if (g.bricks[i] == BRICK_HP1) g.bricks[i] = BRICK_HP2;
    }
}

// REGROW: a few destroyed bricks come back at one hit point. Deliberately
// small, and never in the lowest rows: bricks reappearing just above the paddle
// would be unreactable, and with DESCEND in the table the bottom rows are
// already the dangerous ones.
static void applyRegrow() {
    int16_t slots[BRICK_COUNT];
    int16_t n = 0;
    int16_t limit = (BRICK_ROWS - REGROW_SAFE_ROWS) * BRICK_COLS;

    for (int16_t i = 0; i < limit; ++i) {
        if (g.bricks[i] == BRICK_EMPTY) slots[n++] = i;
    }
    if (n == 0) return;

    for (uint8_t k = 0; k < REGROW_COUNT && n > 0; ++k) {
        int16_t pick = (int16_t)random(n);
        int16_t idx  = slots[pick];
        slots[pick]  = slots[--n];

        g.bricks[idx] = BRICK_HP1;
        int16_t r = idx / BRICK_COLS, c = idx % BRICK_COLS;
        int16_t bx, by;
        brickTopLeft(c, r, bx, by);
        fx_burst(bx + BRICK_W * 0.5f, by + BRICK_H * 0.5f, 4, ROW_COLORS[r], 1.4f);
    }
}

// BARRIER: a row of temporary indestructible blocks seals off the bricks above
// it. Every other negative in the table attacks the paddle, your vision, your
// reaction time or your progress; this one attacks the SPACE, which is a
// genuinely different problem to solve.
//
// Only empty cells are filled, so it never overwrites real bricks, and any cell
// a ball is currently inside is skipped so nothing materialises on top of one.
// The filled indices are remembered so expiry removes exactly what it placed.
static void applyBarrier() {
    if (g.barrierTicks > 0) return;              // already up, do not stack

    int16_t row = BRICK_ROWS / BARRIER_ROW_FRAC;
    g.barrierCount = 0;

    for (int16_t c = 0; c < BRICK_COLS; ++c) {
        int16_t idx = row * BRICK_COLS + c;
        if (g.bricks[idx] != BRICK_EMPTY) continue;

        int16_t bx, by;
        brickTopLeft(c, row, bx, by);

        bool occupied = false;
        for (int16_t i = 0; i < MAX_BALLS; ++i) {
            const Ball& b = g.balls[i];
            if (b.alive && overlaps(b.x, b.y, g.ballSize, g.ballSize,
                                    bx, by, BRICK_W, BRICK_H)) { occupied = true; break; }
        }
        if (occupied) continue;

        g.bricks[idx] = BRICK_SOLID;
        g.barrierCells[g.barrierCount++] = idx;
        fx_burst(bx + BRICK_W * 0.5f, by + BRICK_H * 0.5f, 3, COL_SOLID, 1.5f);
    }

    if (g.barrierCount > 0) { g.barrierTicks = BARRIER_TICKS; fx_shake(3, 12); }
}

static void clearBarrier() {
    for (int16_t i = 0; i < g.barrierCount; ++i) {
        int16_t idx = g.barrierCells[i];
        if (g.bricks[idx] == BRICK_SOLID) g.bricks[idx] = BRICK_EMPTY;
    }
    g.barrierCount = 0;
    g.barrierTicks = 0;
}

static void applyDrop(DropType t) {
    switch (t) {
        // ---- positive ----
        case PU_GROW:      setPaddleWidth(g.paddle.w + PADDLE_W_STEP); break;
        case PU_MULTI:     doMultiBall(); break;
        case PU_STICKY:    g.stickyTicks = STICKY_TICKS; break;
        case PU_DOUBLE:    g.doubleTicks = DOUBLE_TICKS; break;
        case PU_EXPLOSIVE: g.explosiveTicks = EXPLOSIVE_TICKS; break;
        case PU_NET:       g.netActive = true; break;

        case PU_LASER:
            g.laserTicks   = LASER_TICKS;
            g.boltCooldown = 0;
            break;

        case PU_BIGBALL:
            // Timed, and it jumps straight to the cap. Stepping one pixel per
            // catch was the original design and it was invisible on a 320x240
            // panel; a powerup you cannot see is a powerup that does not exist.
            g.bigTicks = BIGBALL_TICKS;
            if (setBallSize(BALL_SIZE_MAX)) {
                // The size change itself is correct and always was; what it
                // lacked was a moment. Burst from each ball plus a shake so the
                // transition is something you see happen rather than something
                // you notice later.
                for (int16_t i = 0; i < MAX_BALLS; ++i) {
                    const Ball& bb = g.balls[i];
                    if (!bb.alive) continue;
                    fx_burst(bb.x + g.ballSize * 0.5f, bb.y + g.ballSize * 0.5f,
                             10, DROPS[PU_BIGBALL].color, 2.6f);
                }
                fx_shake(3, 12);
            }
            break;

        case PU_SLOW:
            // Timed. This used to step a persistent multiplier that nothing
            // ever reset, so one SLOW made the entire rest of the run sluggish.
            setSpeedMul(g.speedMul - SPEED_MUL_STEP);
            g.speedTicks = SPEED_TICKS;
            break;

        case PU_THROUGH: g.throughTicks = THROUGH_TICKS; break;

        case PU_LIFE:
            if (g.lives < MAX_LIVES) ++g.lives;
            break;

        // ---- negative ----
        case PU_SHRINK: setPaddleWidth(g.paddle.w - PADDLE_W_STEP); break;
        case PU_HOLE:   g.holeTicks  = HOLE_TICKS;  break;
        case PU_BARRIER: applyBarrier(); break;
        case PU_FOG:    g.fogTicks   = FOG_TICKS;   break;
        case PU_ARMOR:  applyArmor();  break;
        case PU_REGROW: applyRegrow(); break;

        case PU_FAST:
            setSpeedMul(g.speedMul + SPEED_MUL_STEP);
            g.speedTicks = SPEED_TICKS;
            break;

        case PU_DESCEND:
            g.gridDropTarget += DESCEND_STEP;
            fx_shake(4, 18);
            break;

        default: break;
    }
    DROPS[t].negative ? sfx_powerup_bad() : sfx_powerup_good();

    // The key screen answers "what does T mean" when you have time to look.
    // This answers "what did I just catch", which is where the learning
    // actually happens. Colour matches the pill so the two reinforce.
    float px = g.paddle.x + g.paddle.w * 0.5f;
    fx_popup(px, (float)(PADDLE_Y - 14), DROPS[t].name, DROPS[t].color);

    // The gun silently changes what BtnB means, so say so rather than letting
    // the player discover it by pausing mid-rally.
    if (t == PU_LASER) {
        fx_popup(px, (float)(PADDLE_Y - 30), "TAP B", DROPS[PU_LASER].color);
    }
    if (DROPS[t].negative) fx_shake(2, 8);
}

static void updateDrops() {
    for (int16_t i = 0; i < MAX_DROPS; ++i) {
        Drop& d = g.drops[i];
        if (!d.active) continue;

        d.y += DROP_SPEED;

        if (overlaps(d.x, d.y, DROP_W, DROP_H,
                     g.paddle.x, (float)PADDLE_Y, (float)g.paddle.w, (float)PADDLE_H)) {
            d.active = false;
            applyDrop(d.type);
            continue;
        }
        if (d.y > PLAY_H) d.active = false;
    }
}

// ---------------------------------------------------------------------------
// Level / life flow
// ---------------------------------------------------------------------------
static void startLevel(uint8_t level) {
    g.level = level;
    levels_load(level, g.bricks);
    g.bricksLeft = levels_destructible_remaining(g.bricks);

    g.baseSpeed = DIFFICULTY[g_settings.diff].ballSpeed
                * powf(LEVEL_SPEED_MUL, (float)(level - 1));
    g.baseSpeed = clampf(g.baseSpeed, 0.5f, BALL_SPEED_CAP);

    g.gridDrop = g.gridDropTarget = 0.0f;
    g.crushed  = false;
    g.stickyTicks = g.explosiveTicks = g.laserTicks = 0;
    g.holeTicks = g.fogTicks = g.bigTicks = g.throughTicks = 0;
    g.speedTicks = 0;
    g.speedMul = 1.0f;
    clearBarrier();
    clearBolts();
    // Ball size persists across levels the way paddle width does; the rest of
    // the temporary state does not.
    g.doubleTicks = 0;
    g.netActive = false;
    setBallSize(BALL_SIZE_BASE);
    g.respawnAt = 0;
    g.outroTicks = 0;
    g.introTicks = LEVEL_INTRO_TICKS;   // rows drop in before physics starts
    clearDrops();
    clearBrickFlash();
    fx_reset();
    serveBall();

    hud_set_level(g.level);
    hud_force_redraw();
}

// ---------------------------------------------------------------------------
// Grid teardown, run a few bricks per tick rather than all at once.
//
// Shattering 80 bricks in a single frame would ask for ~240 particles from a
// 64-slot pool: the first dozen bricks would consume it and the rest would emit
// nothing, so the effect would visibly bias to the top-left. Spreading it over
// the outro lets the pool recycle, and the sweep reads as a wave.
// ---------------------------------------------------------------------------
static void outroStep() {
    uint8_t budget = 3;

    for (int16_t i = 0; i < BRICK_COUNT && budget; ++i) {
        uint8_t cell = g.bricks[i];
        if (cell == BRICK_EMPTY) continue;

        int16_t r = i / BRICK_COLS, c = i % BRICK_COLS;
        int16_t bx, by;
        brickTopLeft(c, r, bx, by);

        uint16_t col = (cell == BRICK_SOLID)     ? COL_SOLID
                     : (cell == BRICK_EXPLOSIVE) ? COL_EXPLOSIVE
                                                 : ROW_COLORS[r];

        fx_burst(bx + BRICK_W * 0.5f, by + BRICK_H * 0.5f, 5, col, 2.1f);
        g.bricks[i] = BRICK_EMPTY;
        --budget;
    }
}

static void startGame() {
    g.score     = 0;
    g.beatHigh  = false;
    g.lives     = DIFFICULTY[g_settings.diff].lives;
    g.speedMul  = 1.0f;
    g.serveLean = -1;

    g.paddle.w     = DIFFICULTY[g_settings.diff].paddleW;
    g.paddle.x     = (PLAY_W - g.paddle.w) * 0.5f;
    g.paddle.speed = PADDLE_SPEED_MIN;
    g.paddle.dir   = 0;

    g.paddleFlash = 0;
    g.outroTicks  = 0;
    g.ballSize    = BALL_SIZE_BASE;
    g.bestChain   = 0;
    g.gridDrop    = g.gridDropTarget = 0.0f;
    g.crushed     = false;
    g.doubleTicks = 0;
    g.netActive   = false;

    startLevel(1);

    hud_snap_score(g.score);
    hud_set_lives(g.lives);
    hud_set_high(g.highScore);
    hud_force_redraw();

    setState(ST_PLAYING);
}

static void endGame() {
    // Everything still standing comes apart before the panel appears. Same
    // mechanism as the level-clear sweep, just with a harder shake behind it.
    g.outroTicks = OUTRO_TICKS;
    fx_shake(5, 26);
    for (int16_t i = 0; i < MAX_BALLS; ++i) g.balls[i].alive = false;
    clearDrops();

    if (g.score > g.highScore) {
        g.highScore = g.score;
        g.beatHigh  = true;
        storage_save_high_score(g.highScore);   // one write per session, on death
        hud_set_high(g.highScore);
        sfx_high_score();
    } else {
        sfx_game_over();
    }
    setState(ST_GAME_OVER);
}

static void loseLife() {
    sfx_life_lost();
    if (g.lives > 0) --g.lives;
    hud_set_lives(g.lives);

    if (g.lives == 0) {
        endGame();
    } else {
        g.respawnAt = millis() + RESERVE_MS;
        clearDrops();
    }
}

// ---------------------------------------------------------------------------
// Physics tick
// ---------------------------------------------------------------------------
static void tickPlaying() {
    // Level intro: rows are still dropping in. The paddle moves so you can get
    // set, but nothing else runs. Serving into a half-built grid would let the
    // ball pass through bricks that have not landed yet.
    if (g.introTicks > 0) {
        --g.introTicks;
        updatePaddle();
        for (int16_t i = 0; i < MAX_BALLS; ++i) {
            Ball& b = g.balls[i];
            if (!b.alive || !b.stuck) continue;
            b.x = clampf(g.paddle.x + g.paddle.w * 0.5f + b.stickOffset - g.ballSize * 0.5f,
                         0.0f, (float)(PLAY_W - g.ballSize));
            b.y = (float)(PADDLE_Y - g.ballSize);
        }
        return;
    }

    if (g.paddleFlash > 0) --g.paddleFlash;

    // Timed effects
    if (g.doubleTicks    > 0) --g.doubleTicks;
    if (g.stickyTicks    > 0) --g.stickyTicks;
    if (g.explosiveTicks > 0) --g.explosiveTicks;
    if (g.laserTicks     > 0) --g.laserTicks;
    if (g.holeTicks      > 0) --g.holeTicks;
    if (g.throughTicks   > 0) --g.throughTicks;
    if (g.barrierTicks   > 0 && --g.barrierTicks == 0) clearBarrier();

    // Speed returns to base when the timer runs out, whichever pill set it.
    if (g.speedTicks > 0 && --g.speedTicks == 0) setSpeedMul(1.0f);
    if (g.fogTicks       > 0) --g.fogTicks;
    if (g.boltCooldown   > 0) --g.boltCooldown;
    if (g.laserTicks    == 0) clearBolts();

    // BIG BALL is timed now, so it has to shrink back. Shrinking can never
    // wedge the ball, unlike growing, so no push-out pass is needed here.
    if (g.bigTicks > 0 && --g.bigTicks == 0) setBallSize(BALL_SIZE_BASE);

    updatePaddle();

    // Death pause: paddle still moves so you can reposition, nothing else does.
    if (g.respawnAt != 0) {
        if (millis() >= g.respawnAt) {
            g.respawnAt = 0;
            serveBall();
        }
        return;
    }

    // Grid descent easing. Quadratic-ish approach so it starts moving at once
    // and settles, rather than creeping in at a constant crawl.
    if (g.gridDrop < g.gridDropTarget) {
        float step = (float)DESCEND_STEP / (float)DESCEND_TICKS;
        float gap  = g.gridDropTarget - g.gridDrop;
        g.gridDrop += (gap < step) ? gap : step;
    }

    // bricksLeft is DERIVED, recomputed once per tick, not maintained at the
    // handful of sites that mutate the grid. It used to be incremental, and a
    // bomb clearing the last bricks took an early return past the one line that
    // updated it, so the level could never clear. An 80-byte scan per tick is
    // nothing next to the canvas push and it cannot go stale.
    g.bricksLeft = levels_destructible_remaining(g.bricks);

    // Crush: checked before anything else moves, so the frame you die on is
    // the frame the grid actually arrives.
    int16_t low = lowestBrickBottom();
    if (low >= CRUSH_LINE_Y) {
        g.crushed = true;
        fx_shake(6, 30);
        fx_popup(PLAY_W * 0.5f, (float)(CRUSH_LINE_Y - 20), "CRUSHED", COL_WARN);

        // Crushing ends the run outright rather than costing one life. Any
        // remaining lives are forfeit, which is the point of a loss condition
        // you can see coming for 45 ticks and still failed to clear.
        g.lives = 0;
        hud_set_lives(0);
        sfx_life_lost();
        endGame();
        return;
    }

    updateDrops();

    // --- laser bolts ---
    for (int16_t i = 0; i < MAX_BOLTS; ++i) {
        Bolt& z = s_bolts[i];
        if (!z.active) continue;

        z.y -= BOLT_SPEED;
        if (z.y + BOLT_H < 0) { z.active = false; continue; }

        int16_t c = (int16_t)((z.x - GRID_X0) / BRICK_PITCH_X);
        int16_t r = (int16_t)floorf((z.y - gridTopY()) / (float)BRICK_PITCH_Y);
        if (c < 0 || c >= BRICK_COLS || r < 0 || r >= BRICK_ROWS) continue;

        int16_t bx, by;
        brickTopLeft(c, r, bx, by);
        if (z.x < bx || z.x >= bx + BRICK_W) continue;      // in the gap
        if (z.y > by + BRICK_H) continue;

        if (g.bricks[r * BRICK_COLS + c] != BRICK_EMPTY) {
            damageBrick(r * BRICK_COLS + c, nullptr);       // nullptr: no combo
            fx_burst(z.x, z.y, 4, DROPS[PU_LASER].color, 1.6f);
            z.active = false;
        }
    }

    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        Ball& b = g.balls[i];
        if (!b.alive) continue;

        if (b.stuck) {
            b.x = clampf(g.paddle.x + g.paddle.w * 0.5f + b.stickOffset - g.ballSize * 0.5f,
                         0.0f, (float)(PLAY_W - g.ballSize));
            b.y = (float)(PADDLE_Y - g.ballSize);
            continue;
        }

        // --- horizontal ---
        b.x += b.vx;
        if (b.x < 0)                   { b.x = 0;                       b.vx = -b.vx; sfx_wall(); }
        else if (b.x + g.ballSize > PLAY_W) { b.x = PLAY_W - g.ballSize;  b.vx = -b.vx; sfx_wall(); }
        resolveBricks(b, true);

        // --- vertical ---
        b.y += b.vy;
        if (b.y < 0) { b.y = 0; b.vy = -b.vy; sfx_wall(); }
        resolveBricks(b, false);

        ballVsPaddle(b);

        // The net sits below the paddle and above the kill line, so it only
        // ever catches a ball the paddle already missed. One save, then gone.
        if (g.netActive && b.vy > 0 && b.y + g.ballSize >= NET_Y) {
            b.y = (float)(NET_Y - g.ballSize);
            b.vy = -fabsf(b.vy);
            g.netActive = false;
            sfx_powerup_good();
            fx_burst(b.x, (float)NET_Y, 12, DROPS[PU_NET].color, 2.4f);
            fx_popup(b.x, (float)(NET_Y - 16), "SAVED", DROPS[PU_NET].color);
        }

        // Spin curves the flight first; the anti-stall floor below then gets
        // the FINAL word on vy. Reversed, a strong curve would drive vy toward
        // zero and the floor would shove it back every tick, which shows up as
        // visible jitter rather than as a curve.
        applySpin(b);

        // Anti-stall: a near-horizontal ball skimming the top of the play area
        // can rally forever without ever threatening a brick.
        if (!b.stuck && fabsf(b.vy) < BALL_VY_FLOOR) {
            b.vy = (b.vy < 0 ? -BALL_VY_FLOOR : BALL_VY_FLOOR);
        }

            // Embers off a burning ball. Cheap, and it sells the fire far better
        // than the trail alone.
        if (fireHeat(b) > 0.30f && (g.frameTick % FIRE_EMBER_EVERY) == 0) {
            fx_burst(b.x + g.ballSize * 0.5f, b.y + g.ballSize * 0.5f,
                     1, FIRE_RAMP[1 + (int)(g.frameTick % 3)], 0.8f);
        }

        // Record the position AFTER all collision resolution, so the trail
        // shows where the ball actually went rather than where it would have.
        b.trailHead = (uint8_t)((b.trailHead + 1) % BALL_TRAIL_LEN);
        b.trailX[b.trailHead] = b.x;
        b.trailY[b.trailHead] = b.y;
        if (b.trailCount < BALL_TRAIL_LEN) ++b.trailCount;

        if (b.y > PLAY_H) {
            b.alive = false;
            fx_burst(b.x, b.y, 8, COL_BALL, 2.0f);
        }
    }

    hud_set_score(g.score);

    if (g.score > g.highScore) {
        // Track live once you pass the record, so you can watch it happen.
        hud_set_high(g.score);
    }

    if (g.bricksLeft == 0) {
        sfx_level_clear();
        g.outroTicks = OUTRO_TICKS;
        for (int16_t i = 0; i < MAX_BALLS; ++i) g.balls[i].alive = false;
        clearDrops();
        setState(ST_LEVEL_CLEAR);
        return;
    }

    if (aliveBalls() == 0 && g.respawnAt == 0) {
        fx_shake(3, 12);
        loseLife();
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
// Returns false if this row has not started dropping in yet.
static bool introRowOffset(int16_t r, int16_t& off) {
    if (g.introTicks == 0) { off = 0; return true; }

    uint16_t elapsed = (uint16_t)(LEVEL_INTRO_TICKS - g.introTicks);
    int16_t  p = (int16_t)elapsed - (int16_t)(r * ROW_INTRO_STAGGER);

    if (p <= 0) return false;
    if (p >= (int16_t)ROW_INTRO_RISE) { off = 0; return true; }

    // Quadratic ease-out: fast at the start, settling into place.
    float t = 1.0f - (float)p / (float)ROW_INTRO_RISE;
    off = -(int16_t)(t * t * ROW_INTRO_DROP_PX);
    return true;
}

static void drawBrick(M5Canvas& cv, int16_t c, int16_t r, uint8_t cell, int16_t yOff) {
    int16_t bx, by;
    brickTopLeft(c, r, bx, by);
    by += yOff;

    if (brickFlashing(r * BRICK_COLS + c)) {
        cv.fillRect(bx, by, BRICK_W, BRICK_H, COL_TEXT);
        return;
    }

    uint16_t base;
    uint8_t  segments = 1;

    if (cell == BRICK_SOLID) {
        base = COL_SOLID;
    } else if (cell == BRICK_EXPLOSIVE) {
        base = COL_EXPLOSIVE;
    } else {
        base = ROW_COLORS[r];
        segments = cell;                 // 1, 2 or 3 remaining hits
    }

    cv.fillRect(bx, by, BRICK_W, BRICK_H, base);

    // Bevel. A lit top edge and a shadowed bottom edge turn a flat rectangle
    // into an object, which costs two lines and does more for the look of the
    // playfield than anything else in this function.
    cv.drawFastHLine(bx, by, BRICK_W, lighten565(base, 45));
    cv.drawFastHLine(bx, by + BRICK_H - 1, BRICK_W, dim565(base, 1, 2));

    // Damage: the number of hits still needed, printed on the brick.
    //
    // This replaced a pair of notches cut through the body. Both solve the same
    // problem and they cannot coexist: a 6x8 glyph centred on a 26x10 brick
    // lands exactly where the 2HP notch goes, and 2HP is the most common
    // damaged state there is. A digit is the more precise of the two, so the
    // digit won.
    //
    // 1HP bricks are deliberately left bare. Printing "1" on the large majority
    // of the grid turns the playfield into a spreadsheet, and "no number" is a
    // perfectly learnable way to say "one more hit".
    //
    // White with a dark shadow rather than a shade derived from the brick: the
    // table spans yellow through dark blue, and white-on-shadow is the only
    // combination that stays legible across all eight rows.
    if (segments > 1) {
        char d[2] = { (char)('0' + segments), 0 };
        cv.setFont(&fonts::Font0);
        cv.setTextDatum(textdatum_t::middle_center);
        fx_shadow_string(cv, d, bx + BRICK_W / 2, by + BRICK_H / 2, COL_TEXT);
    }

    if (cell == BRICK_SOLID) {
        // Indestructible: a bright rivet in each corner, so it reads as bolted
        // down rather than as a brick that happens to be grey.
        cv.drawPixel(bx + 2,            by + 2,            COL_TEXT);
        cv.drawPixel(bx + BRICK_W - 3,  by + 2,            COL_TEXT);
        cv.drawPixel(bx + 2,            by + BRICK_H - 3,  COL_TEXT);
        cv.drawPixel(bx + BRICK_W - 3,  by + BRICK_H - 3,  COL_TEXT);
    } else if (cell == BRICK_EXPLOSIVE) {
        cv.drawLine(bx + 7, by + 2, bx + BRICK_W - 8, by + BRICK_H - 3, 0xFFE0);
        cv.drawLine(bx + BRICK_W - 8, by + 2, bx + 7, by + BRICK_H - 3, 0xFFE0);
    }
}

static void drawEffectTimers(M5Canvas& cv) {
    // Two kinds of indicator share this strip: timed effects get a draining
    // bar, and the NET gets a steady letter because "how long" is not a
    // question that applies to a one-shot.
    struct Slot { uint16_t ticks; uint16_t maxTicks; DropType type; };
    const uint8_t NSLOT = 11;
    Slot slots[NSLOT] = {
        { g.stickyTicks,    STICKY_TICKS,    PU_STICKY    },
        { g.doubleTicks,    DOUBLE_TICKS,    PU_DOUBLE    },
        { g.bigTicks,       BIGBALL_TICKS,   PU_BIGBALL   },
        { g.explosiveTicks, EXPLOSIVE_TICKS, PU_EXPLOSIVE },
        { g.laserTicks,     LASER_TICKS,     PU_LASER     },
        { g.holeTicks,      HOLE_TICKS,      PU_HOLE      },
        { g.throughTicks,   THROUGH_TICKS,   PU_THROUGH   },
        { g.barrierTicks,   BARRIER_TICKS,   PU_BARRIER   },
        { g.speedTicks,     SPEED_TICKS,
          (g.speedMul < 1.0f) ? PU_SLOW : PU_FAST },
        { g.fogTicks,       FOG_TICKS,       PU_FOG       },
        { (uint16_t)(g.netActive ? 1 : 0), 0, PU_NET }
    };

    int16_t x = 4;
    cv.setFont(&fonts::Font0);
    cv.setTextDatum(textdatum_t::top_left);

    for (uint8_t i = 0; i < NSLOT; ++i) {
        if (slots[i].ticks == 0) continue;

        uint16_t col = DROPS[slots[i].type].color;
        int16_t  y   = PLAY_H - 10;

        if (slots[i].maxTicks == 0) {
            cv.setTextColor(col);                  // transparent, armed
            char b[2] = { DROPS[slots[i].type].letter, 0 };
            cv.drawString(b, x, y);
            cv.drawFastHLine(x, y + 9, 6, col);
        } else {
            // Blink over the last second and a half so an effect running out is
            // something you notice before it bites.
            bool ending = slots[i].ticks < 90;
            bool on     = !ending || ((g.frameTick / 8) & 1);

            cv.setTextColor(on ? col : dim565(col, 1, 4));
            char b[2] = { DROPS[slots[i].type].letter, 0 };
            cv.drawString(b, x, y);

            int16_t w = (int16_t)(18L * slots[i].ticks / slots[i].maxTicks);
            cv.drawFastHLine(x, y + 9, w, col);
        }
        x += 26;
    }
}

static void drawBallTrail(M5Canvas& cv, const Ball& b, uint16_t base) {
    if (b.trailCount < 2) return;

    float heat = fireHeat(b);

    // Wake: push each sample sideways in proportion to spin and age, so the
    // tail streams off to one side. The orbiting dot is illegible below 6px,
    // which left the 4px default ball with no cue at all except the curve
    // itself; this one reads at any size.
    float wx = 0.0f, wy = 0.0f;
    if (fabsf(b.spin) > SPIN_MIN) {
        float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (sp > 0.01f) {
            wx = ( b.vy / sp) * b.spin * SPIN_WAKE_PX;
            wy = (-b.vx / sp) * b.spin * SPIN_WAKE_PX;
        }
    }

    // Oldest sample first so newer, brighter ones land on top.
    for (uint8_t k = (uint8_t)(b.trailCount - 1); k >= 1; --k) {
        uint8_t idx = (uint8_t)((b.trailHead + BALL_TRAIL_LEN - k) % BALL_TRAIL_LEN);
        uint8_t age = (uint8_t)(k - 1);

        uint16_t col;
        int16_t  sz;

        if (heat > 0.0f) {
            // Burning: walk the flame ramp by age instead of dimming one hue,
            // and jitter each segment sideways so it flickers rather than
            // drawing a clean stripe.
            uint8_t step = (uint8_t)((uint16_t)age * FIRE_STEPS / BALL_TRAIL_LEN);
            if (step >= FIRE_STEPS) step = FIRE_STEPS - 1;
            col = FIRE_RAMP[step];
            if (heat < 0.6f) col = dim565(col, 2, 3);       // barely alight
            sz  = (int16_t)(g.ballSize - age / 2);
            if (sz < 2) sz = 2;
        } else {
            col = dim565(base, (uint16_t)(b.trailCount - k), (uint16_t)(b.trailCount + 2));
            sz  = (age >= 3) ? 2 : 3;
        }

        int16_t jx = 0;
        if (heat > 0.0f && age > 0) {
            jx = (int16_t)((int32_t)((g.frameTick * 7 + idx * 13) % 3) - 1);
        }

        // Age-weighted so the wake fans out behind rather than shifting whole.
        float f = (float)(age + 1) / (float)BALL_TRAIL_LEN;

        cv.fillRect((int16_t)(b.trailX[idx] + wx * f) + (g.ballSize - sz) / 2 + jx,
                    (int16_t)(b.trailY[idx] + wy * f) + (g.ballSize - sz) / 2,
                    sz, sz, col);
    }
}

// Pushes the canvas with the current shake offset applied, then repaints the
// strips the offset exposed. dy is never negative (see fx.h), so the HUD above
// is never touched and never needs a repaint.
static void pushPlay() {
    int16_t dx = 0, dy = 0;
    fx_shake_offset(dx, dy);

    gfx_push(dx, PLAY_Y0 + dy);

    if (dx == 0 && dy == 0) return;

    auto& d = M5.Display;
    if (dx > 0)      d.fillRect(0, PLAY_Y0, dx, PLAY_H, COL_BG);
    else if (dx < 0) d.fillRect(SCREEN_W + dx, PLAY_Y0, -dx, PLAY_H, COL_BG);
    if (dy > 0)      d.fillRect(0, PLAY_Y0, SCREEN_W, dy, COL_BG);
}

static void renderPlay() {
    if (!gfx_ok()) return;
    M5Canvas& cv = gfx_canvas();

    cv.fillSprite(COL_BG);

    for (int16_t r = 0; r < BRICK_ROWS; ++r) {
        int16_t yOff;
        if (!introRowOffset(r, yOff)) continue;

        for (int16_t c = 0; c < BRICK_COLS; ++c) {
            uint8_t cell = g.bricks[r * BRICK_COLS + c];
            if (cell != BRICK_EMPTY) drawBrick(cv, c, r, cell, yOff);
        }
    }

    for (int16_t i = 0; i < MAX_DROPS; ++i) {
        const Drop& d = g.drops[i];
        if (!d.active) continue;

        // Wobble is cosmetic only. d.x stays exactly where updateDrops() put
        // it, so what you see drifting is never what the hitbox is doing.
        float w = sinf((float)(g.frameTick + d.phase) * DROP_WOBBLE_RATE) * DROP_WOBBLE_PX;
        pill_draw(cv, (int16_t)(d.x + w), (int16_t)d.y, DROP_W, DROP_H, d.type, true);
    }

    // Paddle: flashes and grows a pixel on impact, so every bounce registers.
    // Paddle. The HOLE gap is DRAWN, not just collided against: the mechanic
    // was previously invisible, so the ball passed through what looked like a
    // solid paddle, which reads as a bug rather than as a powerup.
    //
    // The two segments are derived from the same HOLE_W the collision test in
    // ballVsPaddle() uses, so what you see is exactly what will catch.
    {
        uint16_t pc  = (g.stickyTicks > 0) ? COL_PADDLE_ST : COL_PADDLE;
        bool     lit = (g.paddleFlash > 0);
        uint16_t col = lit ? COL_TEXT : pc;
        int16_t  px  = (int16_t)g.paddle.x - (lit ? 1 : 0);
        int16_t  py  = PADDLE_Y - (lit ? 1 : 0);
        int16_t  pw  = g.paddle.w + (lit ? 2 : 0);
        int16_t  ph  = PADDLE_H  + (lit ? 2 : 0);

        if (g.holeTicks > 0) {
            int16_t seg = (pw - HOLE_W) / 2;
            if (seg < 2) seg = 2;
            cv.fillRoundRect(px, py, seg, ph, 2, col);
            cv.fillRoundRect(px + pw - seg, py, seg, ph, 2, col);

            // Mark the gap so it reads as a hole rather than as two paddles.
            int16_t gx = px + seg, gw = pw - 2 * seg;
            if (gw > 0) {
                cv.drawFastHLine(gx, py + ph / 2, gw, dim565(DROPS[PU_HOLE].color, 2, 3));
            }
        } else {
            cv.fillRoundRect(px, py, pw, ph, 2, col);
        }
    }

    // Death line. Hidden until the grid has actually started descending: an
    // always-on line would read as scenery, and a loss condition you have
    // stopped noticing is not a warning.
    if (g.gridDropTarget > 0.0f) {
        for (int16_t lx = 0; lx < PLAY_W; lx += 10) {
            cv.fillRect(lx, CRUSH_LINE_Y, 5, 1, dim565(COL_WARN, 2, 3));
        }
    }

    // Laser bolts, under the balls so a ball never hides behind one.
    if (g.laserTicks > 0) {
        for (int16_t i = 0; i < MAX_BOLTS; ++i) {
            if (!s_bolts[i].active) continue;
            cv.fillRect((int16_t)s_bolts[i].x, (int16_t)s_bolts[i].y,
                        BOLT_W, BOLT_H, DROPS[PU_LASER].color);
        }
    }

    // Safety net, drawn under the balls so a ball riding it stays on top.
    if (g.netActive) {
        uint16_t nc = DROPS[PU_NET].color;
        for (int16_t nx = 0; nx < PLAY_W; nx += 8) {
            cv.fillRect(nx, NET_Y, 5, NET_H, nc);
        }
    }

    uint16_t bc = explosiveActive()  ? DROPS[PU_EXPLOSIVE].color
                : (g.throughTicks > 0) ? DROPS[PU_THROUGH].color
                :                        COL_BALL;
    for (int16_t i = 0; i < MAX_BALLS; ++i) {
        const Ball& b = g.balls[i];
        if (!b.alive) continue;
        // Trail takes the chain colour so the rally's state is readable while
        // it is happening, not just when a brick pays out.
        if (!b.stuck) drawBallTrail(cv, b, comboColor(b.combo, bc));
        // A grown ball gets rounded corners so the size change reads as the
        // ball getting fatter rather than the screen getting blockier.
        if (g.ballSize >= 5) {
            cv.fillRoundRect((int16_t)b.x, (int16_t)b.y, g.ballSize, g.ballSize, 2, bc);
        } else {
            cv.fillRect((int16_t)b.x, (int16_t)b.y, g.ballSize, g.ballSize, bc);
        }

        // Spin indicator: a single pixel orbiting at a rate set by the spin, so
        // you can see which way the ball is loaded BEFORE it curves. Only drawn
        // on a fat ball; at the 4px default it is noise rather than signal.
        if (g.ballSize >= SPIN_DOT_MIN_BALL && fabsf(b.spin) > SPIN_MIN) {
            float  r  = g.ballSize * 0.5f - 1.0f;
            float  a  = (float)g.frameTick * b.spin * 0.22f;
            int16_t cx = (int16_t)(b.x + g.ballSize * 0.5f + cosf(a) * r);
            int16_t cy = (int16_t)(b.y + g.ballSize * 0.5f + sinf(a) * r);
            cv.fillRect(cx, cy, 2, 2, COL_BG);
        }
    }

    // FOG: an opaque band drawn over the balls but under the HUD strip and the
    // paddle, so the ball genuinely disappears into it. Nothing mechanical
    // changes, which is what makes it fair: you play the band from memory.
    if (g.fogTicks > 0) {
        cv.fillRect(0, FOG_Y, PLAY_W, FOG_H, COL_BG);
        cv.drawFastHLine(0, FOG_Y, PLAY_W, dim565(DROPS[PU_FOG].color, 1, 2));
        cv.drawFastHLine(0, FOG_Y + FOG_H - 1, PLAY_W, dim565(DROPS[PU_FOG].color, 1, 2));
    }

    drawEffectTimers(cv);
    fx_draw(cv);

    if (g.introTicks > 0) {
        cv.setFont(&fonts::Font4);
        cv.setTextDatum(textdatum_t::middle_center);
        char b[16];
        snprintf(b, sizeof(b), "LEVEL %u", (unsigned)g.level);
        fx_shadow_string(cv, b, PLAY_W / 2, PLAY_H / 2 - 10, COL_ACCENT);
    } else if (g.respawnAt != 0) {
        cv.setFont(&fonts::Font2);
        cv.setTextDatum(textdatum_t::middle_center);
        fx_shadow_string(cv, "BALL LOST", PLAY_W / 2, PLAY_H / 2, COL_WARN);
    } else {
        bool anyStuck = false;
        for (int16_t i = 0; i < MAX_BALLS; ++i) {
            if (g.balls[i].alive && g.balls[i].stuck) { anyStuck = true; break; }
        }
        if (anyStuck) {
            cv.setFont(&fonts::Font2);
            cv.setTextDatum(textdatum_t::middle_center);
            fx_shadow_string(cv, "B to launch", PLAY_W / 2, PLAY_H / 2 + 20, COL_DIM);
        }
    }

    pushPlay();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
uint32_t game_high_score() { return g.highScore; }

void game_clear_high_score() {
    g.highScore = 0;
    g.beatHigh  = false;
    storage_save_high_score(0);
    hud_set_high(0);
}

void game_begin() {
    randomSeed(esp_random());

    memset(&g, 0, sizeof(g));
    g.highScore = storage_load_high_score();
    g.level     = 1;
    g.speedMul  = 1.0f;
    g.serveLean = -1;

    if (!gfx_begin()) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.setTextColor(COL_WARN, COL_BG);
        M5.Display.drawString("OUT OF MEMORY", SCREEN_W / 2, SCREEN_H / 2);
    }
    fx_reset();
    clearBrickFlash();

    hud_begin();
    hud_set_high(g.highScore);
    setState(ST_SPLASH);
}

void game_handle_input() {
    switch (g.state) {

        case ST_SPLASH:
            if (any_pressed() || (millis() - g.stateMs) > SPLASH_MS) {
                menu_reset();
                setState(ST_MENU);
            }
            break;

        case ST_MENU: {
            // Input is swallowed while the toast is up. Drawing it is the
            // menu's own job now: with a full redraw every frame, a toast
            // painted over the top would be wiped on the very next one.
            if (g.toastUntil != 0) {
                if (millis() < g.toastUntil) break;
                g.toastUntil = 0;
            }
            MenuAction a = menu_update();
            if (a == MA_START) {
                startGame();
            } else if (a == MA_SHOW_KEY) {
                g.keyFromPause = false;
                key_reset();
                setState(ST_KEY);
            } else if (a == MA_RESET_HIGH) {
                game_clear_high_score();
                menu_toast("High score cleared", TOAST_MS);
                g.toastUntil = millis() + TOAST_MS;
            }
            break;
        }

        case ST_PLAYING: {
            bool anyStuck = false;
            for (int16_t i = 0; i < MAX_BALLS; ++i) {
                if (g.balls[i].alive && g.balls[i].stuck) { anyStuck = true; break; }
            }

            // BtnB has three jobs while the gun is live. Priority order:
            // launch a held ball, then fire, and pause moves to a long press.
            // A control that silently changes meaning is worse than no control,
            // so catching LASER pops a "TAP B" label on the paddle.
            if (g.laserTicks > 0 && !anyStuck) {
                if (btnB_long_press()) {
                    pause_reset();
                    setState(ST_PAUSED);
                    break;
                }
                if (btnB_pressed() && g.boltCooldown == 0) {
                    fireBolt(g.paddle.x + 3.0f);
                    fireBolt(g.paddle.x + g.paddle.w - 4.0f);
                    g.boltCooldown = BOLT_COOLDOWN;
                    sfx_launch();
                }
                break;
            }

            if (btnB_pressed()) {
                if (anyStuck) {
                    if (g.introTicks == 0) launchStuckBalls();
                } else {
                    pause_reset();
                    setState(ST_PAUSED);
                }
            }
            break;
        }

        case ST_KEY:
            if (key_update()) {
                if (g.keyFromPause) {
                    hud_force_redraw();     // the key screen covered the HUD
                    pause_reset();
                    setState(ST_PAUSED);
                } else {
                    menu_reset();
                    setState(ST_MENU);
                }
            }
            break;

        case ST_PAUSED: {
            MenuAction a = pause_update();
            if (a == MA_RESUME) {
                hud_force_redraw();
                setState(ST_PLAYING);
            } else if (a == MA_SHOW_KEY) {
                g.keyFromPause = true;
                key_reset();
                setState(ST_KEY);
            } else if (a == MA_RESTART) {
                startGame();
            } else if (a == MA_QUIT) {
                menu_reset();
                setState(ST_MENU);
            }
            break;
        }

        case ST_LEVEL_CLEAR:
            // Ignored while the grid is still coming apart, so a mashed button
            // does not skip the animation it just triggered.
            if (g.outroTicks == 0 && btnB_pressed()) {
                startLevel(g.level + 1);
                setState(ST_PLAYING);
            }
            break;

        case ST_GAME_OVER:
            if (g.outroTicks == 0 && btnB_pressed()) {
                menu_reset();
                setState(ST_MENU);
            }
            break;

        default:
            break;
    }
}

void game_tick() {
    ++g.frameTick;

    switch (g.state) {
        case ST_PLAYING: tickPlaying(); break;
        case ST_MENU:    menu_tick();   break;
        case ST_KEY:     key_tick();    break;

        case ST_LEVEL_CLEAR:
        case ST_GAME_OVER:
            if (g.outroTicks > 0) {
                outroStep();
                --g.outroTicks;
                if (g.outroTicks == 0) g.screenDrawn = false;   // panel time
            }
            break;

        default: break;
    }

    tickBrickFlash();
    fx_update();
}

void game_render() {
    switch (g.state) {

        case ST_SPLASH:
            if (!g.screenDrawn) {
                screen_splash(g.highScore, storage_backend_name());
                g.screenDrawn = true;
            }
            break;

        case ST_MENU:
            // Repainted every frame: the attract balls behind the menu move,
            // and the canvas makes a full redraw the same cost as a partial one.
            menu_draw();
            g.screenDrawn = true;
            break;

        case ST_KEY:
            key_draw();
            break;

        case ST_PLAYING:
            renderPlay();
            hud_draw();
            break;

        case ST_PAUSED:
            // Draw the frozen playfield once, then leave the panel alone.
            if (!g.screenDrawn) {
                renderPlay();
                hud_draw();
                g.screenDrawn = true;
                pause_draw(true);
            } else {
                pause_draw(false);
            }
            break;

        case ST_LEVEL_CLEAR:
            if (g.outroTicks > 0) {
                renderPlay();               // the grid shattering
                hud_draw();
            } else if (!g.screenDrawn) {
                renderPlay();
                hud_draw();
                screen_level_clear(g.level, g.score);
                g.screenDrawn = true;
            }
            break;

        case ST_GAME_OVER:
            if (g.outroTicks > 0) {
                renderPlay();
                hud_draw();
            } else if (!g.screenDrawn) {
                screen_game_over(g.score, g.highScore, g.beatHigh, g.bestChain, g.crushed);
                g.screenDrawn = true;
            }
            break;

        default:
            break;
    }
}
