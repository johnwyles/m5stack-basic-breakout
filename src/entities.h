#pragma once
#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
enum GameState : uint8_t {
    ST_SPLASH,
    ST_MENU,
    ST_CONFIRM_RESET,
    ST_PLAYING,
    ST_PAUSED,
    ST_KEY,             // powerup key, reachable from the main and pause menus
    ST_LEVEL_CLEAR,
    ST_GAME_OVER
};

enum Difficulty : uint8_t {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD,
    DIFF_COUNT
};

struct DifficultySpec {
    const char* name;
    float       ballSpeed;    // px per tick
    int16_t     paddleW;
    uint8_t     lives;
};

static const DifficultySpec DIFFICULTY[DIFF_COUNT] = {
    { "Easy",   2.0f, 60, 4 },
    { "Normal", 2.5f, 48, 3 },
    { "Hard",   3.2f, 36, 3 }
};

// ---------------------------------------------------------------------------
enum DropType : uint8_t {
    // --- positive (indices 0 .. PU_NEG_FIRST-1) ---
    PU_GROW = 0,     // wider paddle
    PU_MULTI,        // split into extra balls
    PU_STICKY,       // ball sticks to paddle, BtnB releases
    PU_SLOW,         // slower balls
    PU_LIFE,         // extra life
    PU_DOUBLE,       // 2X score while it lasts
    PU_BIGBALL,      // every ball grows, for a while
    PU_EXPLOSIVE,    // every brick the ball touches detonates
    PU_LASER,        // BtnB fires bolts from the paddle
    PU_THROUGH,      // ploughs 1HP bricks, bounces off anything tougher
    PU_NET,          // one-shot floor across the bottom

    // --- negative (indices PU_NEG_FIRST .. PU_COUNT-1) ---
    PU_SHRINK,       // narrower paddle
    PU_FAST,         // faster balls
    PU_DESCEND,      // whole grid drops one row
    PU_ARMOR,        // every 1HP brick becomes 2HP
    PU_HOLE,         // gap opens in the middle of the paddle
    PU_BARRIER,      // temporary solid blocks seal off the upper field
    PU_FOG,          // dark band the ball disappears behind
    PU_REGROW,       // a few destroyed bricks come back

    PU_COUNT
};

// The table is ordered, not just tagged: everything before PU_NEG_FIRST is
// good and everything from it on is bad. The key screen pages off this
// boundary so a page never straddles the two groups, and pickDropType() uses
// it to scale negative frequency with the level.
static constexpr uint8_t PU_NEG_FIRST = PU_SHRINK;

struct DropSpec {
    char        letter;
    uint16_t    color;
    uint8_t     weight;   // relative drop frequency, before level scaling
    bool        negative;
    const char* name;     // shown on catch and in the key screen
    const char* desc;     // key screen only; keep under 28 chars for Font0
};

// Adding a powerup here is the whole job: the key screen, the catch label and
// the drop table all read from this.
//
// Colour is meaning, so no two entries of opposite valence may share one.
// Same-valence sharing is fine and deliberate; you should be able to tell good
// from bad without reading the letter.
//
// Weights lean positive on purpose. Negatives are roughly a third of the table
// at level 1 and pickDropType() scales them up from there, so the early game
// stays generous and the late game gets mean without the table changing.
static const DropSpec DROPS[PU_COUNT] = {
    // ---- positive ----
    { 'W', 0x07E0, 10, false, "WIDEN",     "Paddle grows wider"     },  // green
    { 'M', 0x07FF, 10, false, "MULTIBALL", "Splits into extra balls"},  // cyan
    { 'C', 0xFFE0, 10, false, "CATCH",     "Ball sticks, B releases"},  // yellow
    { 'S', 0x841F,  7, false, "SLOW",      "Balls slow down, timed" },  // light blue
    { '+', 0xF81F,  4, false, "LIFE",      "One extra life"         },  // magenta
    { 'X', 0xFEA0,  7, false, "DOUBLE",    "2X score while lit"     },  // gold
    { 'G', 0x9FE0, 13, false, "BIG BALL",  "All balls go huge"      },  // lime
    { 'E', 0xFBB8,  7, false, "EXPLOSIVE", "Ball detonates bricks"  },  // pink
    { 'L', 0xBD1F,  8, false, "LASER",     "Tap B to shoot"         },  // violet
    { 'T', 0xFC9F,  8, false, "THROUGH",   "Ploughs 1-hit bricks"   },  // hot pink
    { '=', 0x047F,  5, false, "NET",       "One free missed ball"   },  // azure

    // ---- negative ----
    { 'N', 0xF800,  6, true , "NARROW",    "Paddle shrinks"         },  // red
    { 'F', 0xFD20,  6, true , "FAST",      "Speeds up and ignites"  },  // orange
    { 'V', 0x8000,  4, true , "DESCEND",   "Bricks drop one row"    },  // maroon
    { 'A', 0x7BEF,  5, true , "ARMOR",     "Weak bricks toughen up" },  // steel
    { 'H', 0xC000,  5, true , "HOLE",      "Gap opens in paddle"    },  // dark red
    { 'K', 0x9C60,  5, true , "BARRIER",   "Wall seals off the top"  },  // olive
    { 'Q', 0x5AEB,  4, true , "FOG",       "A band you cannot see"  },  // slate
    { 'Y', 0xA286,  4, true , "REGROW",    "Some bricks come back"  }   // rust
};

// ---------------------------------------------------------------------------
struct Ball {
    bool  alive;
    bool  stuck;        // held on the paddle, waiting for BtnB
    float x, y;         // canvas coords, top-left of the ball rect
    float vx, vy;
    float stickOffset;  // x offset from paddle centre while stuck

    // Recent positions, newest at trailHead. Drawn as a dimming tail, which is
    // a playability win as much as a looks one: at the speed cap a 4px ball is
    // a strobing dot, and the tail is what makes its heading readable.
    float   trailX[BALL_TRAIL_LEN];
    float   trailY[BALL_TRAIL_LEN];
    uint8_t trailHead;
    uint8_t trailCount;

    // Bricks touched since this ball last hit the paddle. Reset by a paddle
    // bounce and by a serve, not by walls or by the safety net.
    uint8_t combo;

    // Signed spin. Positive curves the ball one way, negative the other.
    // Zeroed on serve and on a sticky-paddle catch.
    float   spin;
};

struct Paddle {
    float   x;          // canvas coord, left edge
    int16_t w;
    float   speed;      // current ramped speed
    int8_t  dir;        // -1, 0, +1 as of last tick
};

struct Drop {
    bool     active;
    DropType type;
    float    x, y;      // canvas coords, top-left
    uint8_t  phase;     // wobble offset so a cluster does not move in lockstep
};
