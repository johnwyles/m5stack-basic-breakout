#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Screen / playfield geometry
// ---------------------------------------------------------------------------
static constexpr int16_t SCREEN_W = 320;
static constexpr int16_t SCREEN_H = 240;

static constexpr int16_t HUD_H    = 20;            // top status bar height
static constexpr int16_t PLAY_Y0  = HUD_H;         // first screen row of play area
static constexpr int16_t PLAY_W   = SCREEN_W;
static constexpr int16_t PLAY_H   = SCREEN_H - HUD_H;   // 220

// All in-play coordinates below are CANVAS coordinates (0 == screen row 20).

// ---------------------------------------------------------------------------
// Brick grid
// ---------------------------------------------------------------------------
static constexpr int16_t BRICK_COLS   = 10;
static constexpr int16_t BRICK_ROWS   = 8;
static constexpr int16_t BRICK_COUNT  = BRICK_COLS * BRICK_ROWS;   // 80

static constexpr int16_t BRICK_W      = 28;
static constexpr int16_t BRICK_H      = 10;
static constexpr int16_t BRICK_PITCH_X = 32;       // BRICK_W + 4px gap
static constexpr int16_t BRICK_PITCH_Y = 16;       // BRICK_H + 6px gap

static constexpr int16_t GRID_X0 = 2;              // canvas x of column 0
static constexpr int16_t GRID_Y0 = 10;             // canvas y of row 0 (screen y = 30)

// Brick cell values (also used in the level tables)
static constexpr uint8_t BRICK_EMPTY     = 0;
static constexpr uint8_t BRICK_HP1       = 1;
static constexpr uint8_t BRICK_HP2       = 2;
static constexpr uint8_t BRICK_HP3       = 3;
static constexpr uint8_t BRICK_EXPLOSIVE = 4;
static constexpr uint8_t BRICK_SOLID     = 9;      // indestructible, never decrements

// ---------------------------------------------------------------------------
// Paddle
// ---------------------------------------------------------------------------
static constexpr int16_t PADDLE_H  = 6;
static constexpr int16_t PADDLE_Y  = PLAY_H - 16;  // canvas y == 204  (screen 224)

static constexpr int16_t PADDLE_W_MIN = 24;
static constexpr int16_t PADDLE_W_MAX = 88;
static constexpr int16_t PADDLE_W_STEP = 12;       // grow / shrink increment

// Paddle acceleration: taps are precise, holds are fast.
static constexpr float PADDLE_SPEED_MIN = 1.5f;    // px per tick at press
static constexpr float PADDLE_SPEED_MAX = 5.0f;    // px per tick once ramped
static constexpr float PADDLE_ACCEL     = 0.25f;   // px per tick per tick

// ---------------------------------------------------------------------------
// Ball
// ---------------------------------------------------------------------------
static constexpr int16_t MAX_BALLS = 4;

// Ball size is runtime state now (g.ballSize), not a constant, because the
// BIG BALL pill changes it. It is GLOBAL rather than per-ball: every ball on
// screen is the current size, so catching G with three balls up grows all
// three, and a ball spawned by MULTIBALL afterwards inherits it.
//
// The bounds are not taste, they are geometry:
//   MAX is no longer capped by the brick gap. It used to be: resolveBricks()
//   stopped at the FIRST overlapping brick and pushed the ball flush against
//   it, which parked a wide ball inside the neighbour, and the two fought
//   forever. Growing the ball therefore meant widening the gaps, which made
//   the grid look like a picket fence for a ball that was still small.
//   The resolver now gathers EVERY overlapping brick and resolves against the
//   deepest penetration, so the gap is irrelevant and a wide ball smashing
//   three bricks at once is the feature rather than the bug.
//   MIN is still capped by the paddle. At 2px the ball can pass through the
//   paddle between two frames at the speed cap.
static constexpr int16_t BALL_SIZE_BASE = 4;
static constexpr int16_t BALL_SIZE_MIN  = 3;
static constexpr int16_t BALL_SIZE_MAX  = 12;  // BIG BALL goes straight here

// Two hard geometric rules, both enforced by the static_asserts below:
//   1. The gap between bricks must be at least BALL_SIZE. Otherwise pushing the
//      ball clear of one brick leaves it embedded in the brick stacked behind,
//      and it jitters between the two forever.
//   2. Per-tick travel must stay under the smallest obstacle dimension, or the
//      ball tunnels straight through a brick between two frames.
static constexpr float BALL_SPEED_CAP  = 8.0f;
static constexpr float BALL_VY_FLOOR   = 0.55f;    // anti near-horizontal stall
static constexpr float LEVEL_SPEED_MUL = 1.06f;    // per level
static constexpr float SPEED_MUL_MIN   = 0.60f;    // "slow" powerup floor
static constexpr float SPEED_MUL_MAX   = 1.90f;    // "fast" powerup ceiling
static constexpr float SPEED_MUL_STEP  = 0.15f;

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static constexpr uint32_t TICK_HZ   = 60;
static constexpr uint32_t TICK_MS   = 1000 / TICK_HZ;      // 16ms
static constexpr uint32_t MAX_CATCHUP_TICKS = 5;           // spiral-of-death guard

static constexpr uint32_t SPLASH_MS     = 1500;
static constexpr uint32_t RESERVE_MS    = 900;             // pause after losing a ball
static constexpr uint32_t TOAST_MS      = 1000;            // "High score cleared"

static constexpr uint32_t PERF_REPORT_MS = 3000;

// ---------------------------------------------------------------------------
// Powerup drops
// ---------------------------------------------------------------------------
static constexpr int16_t MAX_DROPS   = 8;
static constexpr int16_t DROP_W      = 16;
static constexpr int16_t DROP_H      = 10;
static constexpr float   DROP_SPEED  = 1.4f;
static constexpr uint8_t DROP_CHANCE = 22;         // percent, on brick destruction

// Visual-only wobble. Applied at draw time, never to d.x, so the hitbox stays
// exactly where the physics thinks it is.
static constexpr float   DROP_WOBBLE_PX   = 2.0f;
static constexpr float   DROP_WOBBLE_RATE = 0.16f;  // radians per tick

// Temporary powerup durations, in ticks
static constexpr uint16_t STICKY_TICKS    = 60 * 12;
static constexpr uint16_t DOUBLE_TICKS    = 60 * 12;
static constexpr uint16_t BIGBALL_TICKS   = 60 * 7;   // short: a 12px ball clears fast
static constexpr uint16_t EXPLOSIVE_TICKS = 60 * 8;
static constexpr uint16_t LASER_TICKS     = 60 * 12;
static constexpr uint16_t HOLE_TICKS      = 60 * 10;
static constexpr uint16_t FOG_TICKS       = 60 * 7;
static constexpr uint16_t THROUGH_TICKS   = 60 * 8;
static constexpr uint16_t BARRIER_TICKS   = 60 * 6;

// SLOW and FAST used to step a persistent multiplier that nothing ever reset,
// so one SLOW made the whole rest of the run sluggish. Both are timed now and
// the multiplier returns to 1.0 on expiry.
static constexpr uint16_t SPEED_TICKS     = 60 * 10;

// --- HOLE: the paddle keeps its width, the middle just stops working ---
static constexpr int16_t HOLE_W = 14;

// --- BARRIER: temporary solid blocks seal off the upper field ---
static constexpr int16_t BARRIER_ROW_FRAC = 2;      // row = BRICK_ROWS / this

// --- FOG: opaque band the ball vanishes behind ---
static constexpr int16_t FOG_Y = (int16_t)(PLAY_H * 0.42f);
static constexpr int16_t FOG_H = 46;

// --- REGROW: deliberately small. Undoing work feels cheap past a handful ---
static constexpr uint8_t REGROW_COUNT = 6;
static constexpr int16_t REGROW_SAFE_ROWS = 2;      // never regrow the lowest rows

// --- LASER: manual fire on a short BtnB press ---
static constexpr int16_t  MAX_BOLTS      = 8;
static constexpr int16_t  BOLT_W         = 2;
static constexpr int16_t  BOLT_H         = 7;
static constexpr float    BOLT_SPEED     = 6.0f;
static constexpr uint16_t BOLT_COOLDOWN  = 14;      // ticks between volleys

// While the gun is live BtnB has three jobs, so pause moves to a long press.
static constexpr uint32_t LONG_PRESS_MS = 400;

// Bricks THROUGH can pass through. Anything tougher stops it, which is what
// keeps it from being a free win: it ploughs weak rows and armour still holds.
static constexpr uint8_t THROUGH_MAX_HP = BRICK_HP1;

// --- Drop table difficulty curve ---
// Negative weights scale with the level so early levels stay generous without
// the table itself changing. Capped so the late game is mean, not unplayable.
static constexpr float NEG_SCALE_PER_LEVEL = 0.08f;
static constexpr float NEG_SCALE_MAX       = 2.0f;

static constexpr int16_t  NET_Y      = PLAY_H - 4;   // safety net line
static constexpr int16_t  NET_H      = 2;


// ---------------------------------------------------------------------------
// Descending grid
//
// One DESCEND pill lowers the whole grid by exactly one row. It is a single
// step, not a repeating sequence: the pill is the event.
//
// The drop is eased over a window rather than applied instantly, so you watch
// it coming down instead of finding it already there. It is implemented as a
// pixel offset applied through one accessor, gridTopY(), used by BOTH the
// drawing and the collision index maths. Shuffling the array instead would be
// two places that have to agree, and they eventually would not.
//
// How many drops a level survives depends on how far down its layout reaches,
// which is the right relationship: a layout that fills the grid is inherently
// closer to the floor than a shallow one.
// ---------------------------------------------------------------------------
static constexpr uint16_t DESCEND_TICKS  = 45;                 // ease window
static constexpr int16_t  DESCEND_STEP   = BRICK_PITCH_Y;      // exactly one row
static constexpr int16_t  CRUSH_LINE_Y   = PADDLE_Y - 4;       // grid reaches here = dead

// ---------------------------------------------------------------------------
// Rally combo
//
// Each brick a ball touches without returning to the paddle raises that ball's
// chain, and every brick in the chain scores COMBO_BONUS_PCT more than the last.
// The counter is PER BALL, not global: with three balls up, each keeps its own
// run and one ball returning to the paddle does not wipe the streak the other
// two are building.
//
// Capped because the ceiling is otherwise unbounded. THROUGH lets a single pass
// plough an entire row without a paddle touch, and stacked with DOUBLE that is
// already 4x on the last brick. Ten steps is a round number that keeps the top
// end at twice base.
// ---------------------------------------------------------------------------
static constexpr uint16_t COMBO_BONUS_PCT = 10;   // extra % per brick in the chain
static constexpr uint16_t COMBO_MAX_STEPS = 10;   // caps the multiplier at 2.0x
static constexpr uint8_t  COMBO_POPUP_MIN = 2;    // show the chain from here up
static constexpr uint8_t  COMBO_TINT_MAX  = 70;   // % lightening at full chain
static constexpr uint8_t  COMBO_CHEER_MIN = 4;    // chain worth announcing at the paddle

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
static constexpr uint8_t LEVEL_COUNT = 10;
static constexpr uint8_t MAX_LIVES   = 9;

// Set to 1 to print one line per paddle hit: incoming vx, paddle velocity,
// slip, resulting spin and degrees of curve per tick. Turn this on before
// changing any value below, so tuning is measurement rather than guesswork.
#define SPIN_DEBUG 1

// ---------------------------------------------------------------------------
// Ball spin  --  THE TUNING BLOCK. Everything about how spin feels is here.
//
// The model is tangential SLIP at contact, not "paddle direction":
//
//     slip = ball.vx - paddleVelocity
//
// which gives the behaviour asked for without special-casing anything. Paddle
// chasing the ball at matching speed leaves near-zero slip and a clean bounce.
// Paddle driving INTO the direction the ball came from gives large slip, heavy
// spin and heavy deflection. A stationary paddle leaves slip equal to the
// ball's own vx, so a mild natural amount.
//
// Paddle velocity is MEASURED from the position delta after clamping, never
// from dir * speed. Holding left while already pinned against the wall means
// the paddle is not moving and must impart nothing; intended velocity would
// give phantom English off the walls.
//
// Spin changes DIRECTION ONLY. The speed is renormalised immediately after the
// Magnus step, because a spin that adds energy would push the ball past
// BALL_SPEED_CAP and straight through bricks between frames.
// ---------------------------------------------------------------------------

// Fraction of slip converted to immediate sideways deflection on the bounce.
// This is the "struck with momentum" feel: a paddle sweeping through the ball
// visibly throws it sideways, separate from the curve that follows. At 0.12 it
// added 0.8 px/tick against a +/-60 degree range set by impact position, so it
// read as noise. Too high and it overrides aiming, which is the primary skill.
// v1 shipped at 0.12 / 0.12 / 0.985 / 0.018 / -0.45 and the curve was
// essentially invisible in play. Two causes, and the second was the real error:
//
//   Decay alone left 30% of the spin after a single traversal, AND
//   SPIN_BOUNCE_RETAIN at -0.45 cut it to 45% on EVERY brick or wall contact.
//   Two contacts put you at 20%, three at 9%. In real play the ball is hitting
//   something constantly, so a max-slip hit deviated 30px on a clean run and
//   only 11px while working through bricks. On a 4px ball that is under three
//   ball widths spread over the whole playfield.
//
// v2 raises everything except FRICTION. Friction is the one competing with
// aiming by impact position, and aiming stays the primary skill.
static constexpr float SPIN_FRICTION = 0.20f;

// Slip -> stored spin. With paddle speed up to 5 and ball vx a few px/tick,
// slip peaks near 7-9, so 0.12 puts a hard counter-move close to SPIN_MAX.
static constexpr float SPIN_GAIN = 0.14f;
static constexpr float SPIN_MAX  = 1.00f;

// Per-tick decay. 0.985 is roughly a 46-tick half-life, so a curve lasts about
// a second and a half rather than forever.
static constexpr float SPIN_DECAY = 0.990f;
static constexpr float SPIN_MIN   = 0.02f;     // below this, just zero it

// Magnus strength: lateral acceleration per unit spin, per tick. This is the
// one that sets how bent the flight looks, and the first value to reach for.
// At v2 settings a max-slip hit deviates about 64px on a clean traversal and
// 25px while working through bricks. Pushed to 0.045 with retention at -0.80
// it reaches 110px, which is a banana rather than a curve; this is close to
// the ceiling before it stops feeling aimable.
static constexpr float SPIN_MAGNUS = 0.050f;

// Walls and bricks flip the sense of the spin and damp it. Negative on purpose.
static constexpr float SPIN_BOUNCE_RETAIN = -0.70f;

// Never let spin eat so much of the velocity that the ball goes flat; the
// anti-stall floor would then fight it every tick and visibly jitter.
static constexpr float SPIN_MAX_VX_FRAC = 0.92f;

// The spin indicator is a single orbiting pixel. Only legible on a fat ball, so
// it appears with BIG BALL and stays off at the 4px default where it is noise.
static constexpr int16_t SPIN_DOT_MIN_BALL = 6;

// The orbiting dot is illegible below 6px, so at the 4px default the only cue
// was the curve itself. The wake fixes that: trail samples are offset sideways
// in proportion to spin and age, so the tail streams off to one side. Reads at
// any ball size and reinforces the arc already on screen.
static constexpr float SPIN_WAKE_PX = 3.2f;

// Spin carried INTO a paddle hit bends the outgoing angle, so spin becomes
// something you set up and then cash in rather than something that merely
// happens to you. Radians of deflection per unit spin.
static constexpr float SPIN_CARRY = 0.30f;

// How much spin survives being cashed in. Most of it is spent turning the
// ball; without this a loaded ball would keep its charge forever.
static constexpr float SPIN_CARRY_CONSUME = 0.30f;

// Hard ceiling on the outgoing paddle angle once position and carry are summed.
// 60 degrees from position plus 17 from carry would leave vy barely above
// BALL_VY_FLOOR, and the anti-stall floor would then fight every bounce.
static constexpr float PADDLE_ANG_MAX = 1.22f;     // 70 degrees

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
static constexpr int16_t FX_PARTICLES  = 64;
static constexpr int16_t FX_POPUPS     = 12;
static constexpr float   FX_GRAVITY    = 0.14f;
static constexpr float   FX_DRAG       = 0.985f;
static constexpr uint8_t FX_PART_LIFE  = 30;
static constexpr uint8_t FX_POPUP_LIFE = 44;
static constexpr float   FX_POPUP_RISE = 0.34f;    // px per tick
static constexpr uint8_t FX_SHAKE_CAP  = 6;        // px, hard ceiling
static constexpr uint16_t FX_SHADOW_COL = 0x1082;  // near-black, for text shadows

static constexpr uint8_t BALL_TRAIL_LEN     = 8;   // long enough to read as flame

// Heat is measured from the ball's ACTUAL velocity against the level's base
// speed, not from the speed multiplier. Two reasons: THROUGH lights the ball
// without touching speed at all, and gating the flame on a pill you might never
// catch meant it could go a whole run without ever appearing.
static constexpr float FIRE_THRESHOLD = 1.08f;   // ratio to base before it lights
static constexpr uint8_t FIRE_STEPS = 5;
static constexpr uint16_t FIRE_RAMP[FIRE_STEPS] = {
    0xFFFF,  // white hot at the ball
    0xFFE0,  // yellow
    0xFD20,  // orange
    0xF800,  // red
    0x7000   // dying ember
};
static constexpr uint8_t FIRE_EMBER_EVERY = 5;    // ticks between ember puffs
static constexpr uint8_t PADDLE_FLASH_TICKS = 4;
static constexpr uint8_t BRICK_FLASH_TICKS  = 3;
static constexpr int16_t BRICK_FLASH_SLOTS  = 6;   // multiball can hit several at once

// Level intro: rows drop in from above, staggered top to bottom.
static constexpr uint16_t ROW_INTRO_STAGGER = 4;   // ticks between row starts
static constexpr uint16_t ROW_INTRO_RISE    = 26;  // ticks for one row to settle
static constexpr int16_t  ROW_INTRO_DROP_PX = 70;
static constexpr uint16_t LEVEL_INTRO_TICKS =
    (BRICK_ROWS - 1) * ROW_INTRO_STAGGER + ROW_INTRO_RISE + 6;

// Level clear / game over: everything left on the grid shatters first.
static constexpr uint16_t OUTRO_TICKS = 54;

static constexpr uint8_t SCORE_ROLL_DIVISOR = 6;   // HUD counts up, not jumps

// Brick damage is shown as the number of hits still required, printed on the
// brick. Bricks needing one more hit show nothing, so only damaged or armoured
// bricks carry a digit and the grid stays readable.
static constexpr uint8_t ATTRACT_BALLS      = 5;

// ---------------------------------------------------------------------------
// Colors (RGB565; the 8-bit canvas converts on write)
// ---------------------------------------------------------------------------
static constexpr uint16_t COL_BG        = 0x0000;
static constexpr uint16_t COL_HUD_BG    = 0x2104;
static constexpr uint16_t COL_TEXT      = 0xFFFF;
static constexpr uint16_t COL_DIM       = 0x8410;
static constexpr uint16_t COL_ACCENT    = 0x07FF;
static constexpr uint16_t COL_PADDLE    = 0xFFFF;
static constexpr uint16_t COL_PADDLE_ST = 0x07E0;  // sticky paddle
static constexpr uint16_t COL_BALL      = 0xFFFF;
// Through-ball violet. This used to be 0xFD20, the same orange as the FAST
// pill, which meant one colour stood for both "good" and "bad". A legend
// screen makes that collision worse, not better: it puts the two identical
// swatches side by side and tells the player the colour means nothing. The
// pill and the tinted ball share this value so the effect reads as one thing.
static constexpr uint16_t COL_BALL_THRU = 0xBD1F;
static constexpr uint16_t COL_SOLID     = 0x7BEF;
static constexpr uint16_t COL_EXPLOSIVE = 0xF800;
static constexpr uint16_t COL_LIFE      = 0x07E0;
static constexpr uint16_t COL_WARN      = 0xF800;

// Row colors, top row first
static constexpr uint16_t ROW_COLORS[BRICK_ROWS] = {
    0xF81F,  // magenta
    0xF800,  // red
    0xFD20,  // orange
    0xFFE0,  // yellow
    0x07E0,  // green
    0x07FF,  // cyan
    0x041F,  // blue
    0x8410   // grey
};

// Row score values, top row first
static constexpr uint16_t ROW_POINTS[BRICK_ROWS] = {
    80, 70, 60, 50, 40, 30, 20, 10
};

// ---------------------------------------------------------------------------
// Scales an RGB565 colour by num/den. Used for trails, particle burn-out and
// popup fades. There is no cheap alpha blend on an 8-bit palette canvas, so a
// fade is really a walk down a ramp of darker colours. At 256 palette entries
// it bands slightly on the darkest steps, which at this pixel size reads as a
// fade rather than as an artifact.
// ---------------------------------------------------------------------------
static inline uint16_t dim565(uint16_t c, uint16_t num, uint16_t den) {
    if (den == 0) return COL_BG;
    if (num >= den) return c;
    uint16_t r = (uint16_t)((c >> 11) & 0x1F);
    uint16_t g = (uint16_t)((c >>  5) & 0x3F);
    uint16_t b = (uint16_t)( c        & 0x1F);
    r = (uint16_t)((uint32_t)r * num / den);
    g = (uint16_t)((uint32_t)g * num / den);
    b = (uint16_t)((uint32_t)b * num / den);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Mixes a colour toward white by pct percent. The counterpart to dim565(),
// used for the lit top edge of a brick bevel.
static inline uint16_t lighten565(uint16_t c, uint8_t pct) {
    uint16_t r = (uint16_t)((c >> 11) & 0x1F);
    uint16_t g = (uint16_t)((c >>  5) & 0x3F);
    uint16_t b = (uint16_t)( c        & 0x1F);
    r = (uint16_t)(r + (uint32_t)(31 - r) * pct / 100);
    g = (uint16_t)(g + (uint32_t)(63 - g) * pct / 100);
    b = (uint16_t)(b + (uint32_t)(31 - b) * pct / 100);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// ---------------------------------------------------------------------------
// Geometry invariants. These are not style preferences; violating either one
// produces collision bugs that only show up after several minutes of play.
// ---------------------------------------------------------------------------
// No gap-vs-ball assert any more: the multi-overlap resolver removed that
// constraint. The gap only needs to be positive so bricks read as separate.
static_assert(BRICK_PITCH_X > BRICK_W, "bricks would touch horizontally");
static_assert(BRICK_PITCH_Y > BRICK_H, "bricks would touch vertically");
static_assert(BALL_SPEED_CAP < BRICK_H,
              "ball could tunnel through a brick in one tick");
static_assert(BALL_SPEED_CAP < PADDLE_H + BALL_SIZE_MIN,
              "the SMALLEST ball could tunnel through the paddle in one tick");
static_assert(BALL_SIZE_MIN <= BALL_SIZE_BASE && BALL_SIZE_BASE <= BALL_SIZE_MAX,
              "default ball size is outside its own range");
static_assert(GRID_Y0 + BRICK_ROWS * BRICK_PITCH_Y < PADDLE_Y,
              "brick grid reaches the paddle");
static_assert(GRID_X0 + BRICK_COLS * BRICK_PITCH_X - (BRICK_PITCH_X - BRICK_W) <= PLAY_W,
              "brick grid is wider than the play area");
static_assert(LEVEL_INTRO_TICKS >= (BRICK_ROWS - 1) * ROW_INTRO_STAGGER + ROW_INTRO_RISE,
              "intro ends before the bottom row has finished dropping in");
