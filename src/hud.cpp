#include "hud.h"
#include <M5Unified.h>
#include "config.h"
#include "storage.h"

// Field rectangles inside the 320x20 bar.
static const int16_t F_SCORE_X = 2,   F_SCORE_W = 96;
static const int16_t F_HIGH_X  = 108, F_HIGH_W  = 78;
static const int16_t F_LEVEL_X = 196, F_LEVEL_W = 40;
static const int16_t F_LIVES_X = 242, F_LIVES_W = 62;
static const int16_t F_WARN_X  = 308, F_WARN_W  = 10;

static const int16_t LIFE_W = 8, LIFE_H = 8, LIFE_GAP = 3;
static const uint8_t LIVES_SHOWN_MAX = 5;

static uint32_t s_score = 0, s_high = 0;   // s_score is what is DRAWN
static uint32_t s_scoreTarget = 0;         // what the game has actually scored
static uint8_t  s_level = 1, s_lives = 0;
static bool     s_dScore = true, s_dHigh = true, s_dLevel = true, s_dLives = true;
static bool     s_dFrame = true;

static void pad5(uint32_t v, char* out) {
    if (v > 99999UL) v = 99999UL;
    snprintf(out, 8, "%05lu", (unsigned long)v);
}

static void clearField(int16_t x, int16_t w) {
    M5.Display.fillRect(x, 0, w, HUD_H, COL_HUD_BG);
}

void hud_begin() {
    hud_force_redraw();
}

void hud_force_redraw() {
    s_dFrame = s_dScore = s_dHigh = s_dLevel = s_dLives = true;
}

// The HUD is the one thing here NOT drawn into the canvas: it lives on the
// direct framebuffer with per-field dirty flags. That makes the rolling score
// the only animation in the project with a real per-frame cost, since it
// repaints its ~96px field on every frame it is moving. Bounded and fine, but
// worth knowing it is not free the way the canvas effects are.

void hud_set_score(uint32_t v) { s_scoreTarget = v; }

void hud_snap_score(uint32_t v) {
    s_scoreTarget = v;
    if (v != s_score) { s_score = v; s_dScore = true; }
}

// Walks the drawn score toward the target. The step scales with the gap, so a
// 40-point brick ticks over in a few frames and a 250-point bonus sweeps, but
// neither ever takes long enough to still be counting when the next brick dies.
static void rollScore() {
    if (s_score == s_scoreTarget) return;

    if (s_scoreTarget < s_score) {          // only happens on a reset we missed
        s_score = s_scoreTarget;
    } else {
        uint32_t gap  = s_scoreTarget - s_score;
        uint32_t step = gap / SCORE_ROLL_DIVISOR;
        if (step < 1) step = 1;
        s_score += step;
    }
    s_dScore = true;
}
void hud_set_high (uint32_t v) { if (v != s_high ) { s_high  = v; s_dHigh  = true; } }
void hud_set_level(uint8_t  v) { if (v != s_level) { s_level = v; s_dLevel = true; } }
void hud_set_lives(uint8_t  v) { if (v != s_lives) { s_lives = v; s_dLives = true; } }

void hud_draw() {
    auto& d = M5.Display;
    char buf[16];

    rollScore();

    if (s_dFrame) {
        d.fillRect(0, 0, SCREEN_W, HUD_H, COL_HUD_BG);
        d.drawFastHLine(0, HUD_H - 1, SCREEN_W, COL_DIM);
        s_dFrame = false;
    }

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::middle_left);

    if (s_dScore) {
        clearField(F_SCORE_X, F_SCORE_W);
        pad5(s_score, buf);
        d.setTextColor(COL_DIM, COL_HUD_BG);
        d.drawString("SCORE", F_SCORE_X, HUD_H / 2 - 1);
        d.setTextColor(COL_TEXT, COL_HUD_BG);
        d.drawString(buf, F_SCORE_X + 44, HUD_H / 2 - 1);
        s_dScore = false;
    }

    if (s_dHigh) {
        clearField(F_HIGH_X, F_HIGH_W);
        pad5(s_high, buf);
        d.setTextColor(COL_DIM, COL_HUD_BG);
        d.drawString("HI", F_HIGH_X, HUD_H / 2 - 1);
        d.setTextColor(COL_ACCENT, COL_HUD_BG);
        d.drawString(buf, F_HIGH_X + 20, HUD_H / 2 - 1);
        s_dHigh = false;
    }

    if (s_dLevel) {
        clearField(F_LEVEL_X, F_LEVEL_W);
        snprintf(buf, sizeof(buf), "LV%u", (unsigned)s_level);
        d.setTextColor(COL_TEXT, COL_HUD_BG);
        d.drawString(buf, F_LEVEL_X, HUD_H / 2 - 1);
        s_dLevel = false;
    }

    if (s_dLives) {
        clearField(F_LIVES_X, F_LIVES_W);
        // Lives render as small blocks; reading a count of pips is faster than
        // reading a digit. Beyond five we fall back to "xN".
        if (s_lives <= LIVES_SHOWN_MAX) {
            for (uint8_t i = 0; i < s_lives; ++i) {
                int16_t x = F_LIVES_X + i * (LIFE_W + LIFE_GAP);
                M5.Display.fillRect(x, (HUD_H - LIFE_H) / 2 - 1, LIFE_W, LIFE_H, COL_LIFE);
            }
        } else {
            M5.Display.fillRect(F_LIVES_X, (HUD_H - LIFE_H) / 2 - 1, LIFE_W, LIFE_H, COL_LIFE);
            snprintf(buf, sizeof(buf), "x%u", (unsigned)s_lives);
            d.setTextColor(COL_LIFE, COL_HUD_BG);
            d.drawString(buf, F_LIVES_X + LIFE_W + 4, HUD_H / 2 - 1);
        }
        s_dLives = false;
    }

    // Persistent marker: saving is off, so nothing you do here will survive
    // a power cycle. Better to show it than to silently lose records.
    if (!storage_available()) {
        clearField(F_WARN_X, F_WARN_W);
        M5.Display.fillCircle(F_WARN_X + 4, HUD_H / 2 - 1, 3, COL_WARN);
    }
}
