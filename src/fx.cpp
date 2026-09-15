#include "fx.h"
#include <math.h>

struct Particle {
    bool     active;
    float    x, y, vx, vy;
    uint8_t  life, maxLife;
    uint16_t col;
};

struct Popup {
    bool     active;
    float    x, y;
    uint8_t  life, maxLife;
    uint16_t col;
    char     text[12];
};

static Particle s_parts[FX_PARTICLES];
static Popup    s_pops[FX_POPUPS];

static uint8_t s_shakeAmp   = 0;
static uint8_t s_shakeTicks = 0;
static int16_t s_shakeDx    = 0;
static int16_t s_shakeDy    = 0;

// ---------------------------------------------------------------------------
void fx_reset() {
    for (int16_t i = 0; i < FX_PARTICLES; ++i) s_parts[i].active = false;
    for (int16_t i = 0; i < FX_POPUPS;    ++i) s_pops[i].active  = false;
    s_shakeAmp = s_shakeTicks = 0;
    s_shakeDx  = s_shakeDy    = 0;
}

// ---------------------------------------------------------------------------
void fx_burst(float x, float y, uint8_t count, uint16_t col, float spread) {
    uint8_t made = 0;

    for (int16_t i = 0; i < FX_PARTICLES && made < count; ++i) {
        Particle& p = s_parts[i];
        if (p.active) continue;

        // Biased upward: debris thrown off a brick should arc, not spray
        // sideways, or a shattered row looks like a horizontal smear.
        float ang = ((float)random(1000) / 1000.0f) * 2.0f * PI;
        float mag = spread * (0.45f + (float)random(1000) / 1800.0f);

        p.active  = true;
        p.x       = x;
        p.y       = y;
        p.vx      = cosf(ang) * mag;
        p.vy      = sinf(ang) * mag - spread * 0.4f;
        p.maxLife = (uint8_t)(FX_PART_LIFE - random(8));
        p.life    = p.maxLife;
        p.col     = col;
        ++made;
    }
}

// ---------------------------------------------------------------------------
void fx_popup(float x, float y, const char* text, uint16_t col) {
    for (int16_t i = 0; i < FX_POPUPS; ++i) {
        Popup& p = s_pops[i];
        if (p.active) continue;

        p.active  = true;
        p.x       = x;
        p.y       = y;
        p.maxLife = FX_POPUP_LIFE;
        p.life    = FX_POPUP_LIFE;
        p.col     = col;
        strncpy(p.text, text, sizeof(p.text) - 1);
        p.text[sizeof(p.text) - 1] = 0;
        return;
    }
    // Pool full: dropped on purpose. See the header.
}

void fx_popup_points(float x, float y, uint32_t pts, uint16_t col) {
    char b[12];
    snprintf(b, sizeof(b), "+%lu", (unsigned long)pts);
    fx_popup(x, y, b, col);
}

// ---------------------------------------------------------------------------
void fx_shake(uint8_t amplitude, uint8_t ticks) {
    if (amplitude > FX_SHAKE_CAP) amplitude = FX_SHAKE_CAP;
    if (amplitude > s_shakeAmp)   s_shakeAmp   = amplitude;
    if (ticks     > s_shakeTicks) s_shakeTicks = ticks;
}

// ---------------------------------------------------------------------------
void fx_update() {
    for (int16_t i = 0; i < FX_PARTICLES; ++i) {
        Particle& p = s_parts[i];
        if (!p.active) continue;

        p.vy += FX_GRAVITY;
        p.vx *= FX_DRAG;
        p.x  += p.vx;
        p.y  += p.vy;

        if (--p.life == 0 || p.y > PLAY_H + 8) p.active = false;
    }

    for (int16_t i = 0; i < FX_POPUPS; ++i) {
        Popup& p = s_pops[i];
        if (!p.active) continue;

        p.y -= FX_POPUP_RISE;
        if (--p.life == 0) p.active = false;
    }

    if (s_shakeTicks > 0) {
        --s_shakeTicks;

        // Decay with the remaining time so the shake settles instead of
        // stopping dead, and re-roll the offset each tick.
        uint8_t amp = (uint8_t)((uint16_t)s_shakeAmp * s_shakeTicks / 8);
        if (amp > s_shakeAmp) amp = s_shakeAmp;

        if (amp == 0) {
            s_shakeDx = s_shakeDy = 0;
        } else {
            s_shakeDx = (int16_t)(random(amp * 2 + 1) - amp);
            s_shakeDy = (int16_t)random(amp + 1);
        }
        if (s_shakeTicks == 0) { s_shakeAmp = 0; s_shakeDx = s_shakeDy = 0; }
    }
}

void fx_shake_offset(int16_t& dx, int16_t& dy) {
    dx = s_shakeDx;
    dy = s_shakeDy;
}

// ---------------------------------------------------------------------------
void fx_shadow_string(M5Canvas& c, const char* text, int16_t x, int16_t y, uint16_t col) {
    c.setTextColor(FX_SHADOW_COL);          // one arg == transparent background
    c.drawString(text, x + 1, y + 1);
    c.setTextColor(col);
    c.drawString(text, x, y);
}

// ---------------------------------------------------------------------------
void fx_draw(M5Canvas& c) {
    for (int16_t i = 0; i < FX_PARTICLES; ++i) {
        const Particle& p = s_parts[i];
        if (!p.active) continue;

        // Burn down through the colour ramp over the particle's life.
        uint16_t col = dim565(p.col, p.life, p.maxLife);
        c.fillRect((int16_t)p.x, (int16_t)p.y, 2, 2, col);
    }

    c.setFont(&fonts::Font0);
    c.setTextDatum(textdatum_t::middle_center);

    for (int16_t i = 0; i < FX_POPUPS; ++i) {
        const Popup& p = s_pops[i];
        if (!p.active) continue;

        // Hold full brightness for the first third, then fade. Fading from
        // frame one makes the number hard to read at exactly the moment the
        // player is looking for it.
        uint16_t num = p.life;
        uint16_t den = (uint16_t)(p.maxLife * 2 / 3);
        uint16_t col = (num >= den) ? p.col : dim565(p.col, num, den);

        fx_shadow_string(c, p.text, (int16_t)p.x, (int16_t)p.y, col);
    }
}

// ---------------------------------------------------------------------------
bool fx_busy() {
    for (int16_t i = 0; i < FX_PARTICLES; ++i) if (s_parts[i].active) return true;
    for (int16_t i = 0; i < FX_POPUPS;    ++i) if (s_pops[i].active)  return true;
    return false;
}
