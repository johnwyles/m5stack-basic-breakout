#include "levels.h"

// Each level is 8 rows of 10 characters, top row first.
//   .  empty        1/2/3  brick with that many hit points
//   X  explosive    S      indestructible
// Empty rows go at the BOTTOM, never the top. Two things depend on it: the
// grid should start hard against the HUD rather than floating 40px below it,
// and headroom above the bricks is the space DESCEND eats. A layout that gives
// its top rows away starts the level already partway to the floor.
static const char L01[] PROGMEM =
    "1111111111"
    "1111111111"
    "1111111111"
    "1111111111"
    ".........."
    ".........."
    ".........."
    "..........";

static const char L02[] PROGMEM =
    "2222222222"
    "1111111111"
    "1111111111"
    "2222222222"
    "1111111111"
    "1111111111"
    ".........."
    "..........";

static const char L03[] PROGMEM =
    "....11...."
    "...1111..."
    "..111111.."
    ".11111111."
    "1111111111"
    ".22222222."
    "..222222.."
    "...X..X...";

static const char L04[] PROGMEM =
    "S........S"
    ".11111111."
    ".12222221."
    ".12333321."
    ".12333321."
    ".12222221."
    ".11111111."
    "S........S";

static const char L05[] PROGMEM =
    "1.1.1.1.1."
    ".2.2.2.2.2"
    "1.1.1.1.1."
    ".2.2.2.2.2"
    "1.1.1.1.1."
    ".2.2.2.2.2"
    "X........X"
    "..........";

static const char L06[] PROGMEM =
    "SS......SS"
    "2211111112"
    "2........2"
    "2..X..X..2"
    "2........2"
    "2111111112"
    "2222222222"
    "..........";

static const char L07[] PROGMEM =
    "3........3"
    ".3......3."
    "..3....3.."
    "...3..3..."
    "...2..2..."
    "..2....2.."
    ".2......2."
    "2........2";

static const char L08[] PROGMEM =
    "1111111111"
    "1SSSSSSSS1"
    "1122222211"
    "112X..X211"
    "112....211"
    "1122222211"
    "1SSSSSSSS1"
    "1111111111";

static const char L09[] PROGMEM =
    "..333333.."
    ".33333333."
    "33S3333S33"
    "3333333333"
    ".22222222."
    "..222222.."
    "...X..X..."
    "....11....";

static const char L10[] PROGMEM =
    "SSSSSSSSSS"
    "3333333333"
    "33X3333X33"
    "3333333333"
    "22S2222S22"
    "2222222222"
    "11X1111X11"
    "1111111111";

static const char* const LEVEL_TABLE[LEVEL_COUNT] PROGMEM = {
    L01, L02, L03, L04, L05, L06, L07, L08, L09, L10
};

static uint8_t charToCell(char c) {
    switch (c) {
        case '1': return BRICK_HP1;
        case '2': return BRICK_HP2;
        case '3': return BRICK_HP3;
        case 'X': return BRICK_EXPLOSIVE;
        case 'S': return BRICK_SOLID;
        default:  return BRICK_EMPTY;   // '.' and anything unexpected
    }
}

void levels_load(uint8_t level, uint8_t* dst) {
    if (level < 1) level = 1;

    // Levels wrap. Each full pass through the table adds a hit point to the
    // upper half of the grid, so the same layouts get meaner on repeat.
    uint8_t lap   = (level - 1) / LEVEL_COUNT;
    uint8_t index = (level - 1) % LEVEL_COUNT;

    const char* src = (const char*)pgm_read_ptr(&LEVEL_TABLE[index]);

    for (int16_t i = 0; i < BRICK_COUNT; ++i) {
        uint8_t cell = charToCell((char)pgm_read_byte(src + i));

        if (lap > 0 && cell >= BRICK_HP1 && cell <= BRICK_HP3) {
            int16_t row = i / BRICK_COLS;
            if (row < BRICK_ROWS / 2) {
                uint16_t bumped = cell + lap;
                cell = (uint8_t)(bumped > BRICK_HP3 ? BRICK_HP3 : bumped);
            }
        }
        dst[i] = cell;
    }
}

uint16_t levels_destructible_remaining(const uint8_t* src) {
    uint16_t n = 0;
    for (int16_t i = 0; i < BRICK_COUNT; ++i) {
        if (src[i] != BRICK_EMPTY && src[i] != BRICK_SOLID) ++n;
    }
    return n;
}
