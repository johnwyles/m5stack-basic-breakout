#pragma once
#include <Arduino.h>

// Short non-blocking blips on the internal speaker. Every call is a no-op when
// sound is off, so callers never need to guard.

void audio_begin();
void audio_set_enabled(bool on);
bool audio_enabled();

void sfx_startup();
void sfx_menu_move();
void sfx_menu_select();
void sfx_launch();
void sfx_paddle();
void sfx_wall();
void sfx_brick_hit();        // damaged but not destroyed
void sfx_brick_break();
void sfx_brick_solid();      // bounced off an indestructible brick
void sfx_explode();
void sfx_powerup_good();
void sfx_powerup_bad();
void sfx_life_lost();
void sfx_level_clear();
void sfx_game_over();
void sfx_high_score();
