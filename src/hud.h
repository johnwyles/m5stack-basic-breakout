#pragma once
#include <Arduino.h>

// Top status bar. Each field tracks its own dirty flag, so a score bump
// repaints ~50px instead of the whole bar, and nothing repaints at all on a
// frame where no value changed.

void hud_begin();
void hud_force_redraw();          // repaint everything on the next hud_draw()

// Sets the score TARGET. The displayed number counts up to it over a few
// frames, because a number that animates is a number the eye follows. Use
// hud_snap_score() when the value jumps for a non-gameplay reason (new game,
// restart), where rolling would just look like a bug.
void hud_set_score(uint32_t v);
void hud_snap_score(uint32_t v);
void hud_set_high(uint32_t v);
void hud_set_level(uint8_t v);
void hud_set_lives(uint8_t v);

void hud_draw();
