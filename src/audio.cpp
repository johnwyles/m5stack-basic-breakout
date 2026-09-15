#include "audio.h"
#include <M5Unified.h>

// The Basic's speaker is harsh and loud. Keep this low.
static const uint8_t SPK_VOLUME = 48;   // 0..255
static const int     SPK_CHANNEL = 0;

static bool s_enabled = true;

void audio_begin() {
    M5.Speaker.setVolume(SPK_VOLUME);
}

void audio_set_enabled(bool on) {
    s_enabled = on;
    if (!on) M5.Speaker.stop();
}

bool audio_enabled() { return s_enabled; }

// First note of an effect cuts whatever was playing. Follow-up notes queue on
// the same channel, otherwise a two-note effect would only ever play its
// second note.
static inline void blip(uint16_t freq, uint32_t ms) {
    if (!s_enabled) return;
    M5.Speaker.tone((float)freq, ms, SPK_CHANNEL, true);
}

static inline void next(uint16_t freq, uint32_t ms) {
    if (!s_enabled) return;
    M5.Speaker.tone((float)freq, ms, SPK_CHANNEL, false);
}

void sfx_startup()      { blip(660, 70);  next(990, 90); }
void sfx_menu_move()    { blip(520, 18); }
void sfx_menu_select()  { blip(880, 45); }
void sfx_launch()       { blip(740, 40); }
void sfx_paddle()       { blip(420, 22); }
void sfx_wall()         { blip(300, 18); }
void sfx_brick_hit()    { blip(560, 16); }
void sfx_brick_break()  { blip(820, 26); }
void sfx_brick_solid()  { blip(180, 22); }
void sfx_explode()      { blip(140, 90); }
void sfx_powerup_good() { blip(980, 50); }
void sfx_powerup_bad()  { blip(220, 60); }
void sfx_life_lost()    { blip(330, 90);  next(200, 140); }
void sfx_level_clear()  { blip(660, 70);  next(880, 70);  next(1180, 120); }
void sfx_game_over()    { blip(400, 120); next(300, 140); next(180, 240); }
void sfx_high_score()   { blip(880, 60);  next(1100, 60); next(1320, 60); next(1760, 160); }
