#pragma once
#include <Arduino.h>
#include "entities.h"

// Session settings. Not persisted in v1; only the high score is.
struct Settings {
    Difficulty diff;
    bool       sound;
};
extern Settings g_settings;

void game_begin();

// Called once per loop, before the tick block. Consumes button edge events.
void game_handle_input();

// Fixed 60Hz physics step. Called zero or more times per loop.
void game_tick();

// Called once per loop after ticking.
void game_render();

// Queried by the menu so it can show the current record and grey out reset.
uint32_t game_high_score();
void     game_clear_high_score();
