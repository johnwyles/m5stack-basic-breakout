#pragma once
#include <Arduino.h>
#include <M5Unified.h>

// ---------------------------------------------------------------------------
// The 320x220 8-bit canvas used by both the playfield and the menus.
//
// It used to live as a static inside game.cpp. It moved here because the menu
// wants it too (attract-mode balls need a full redraw every frame, which is
// only cheap when you already own a back buffer).
//
// Coordinate convention: canvas (0,0) is the top-left of the *canvas*, not the
// screen. Gameplay pushes it at y = PLAY_Y0 so the HUD survives above it.
// Menus push it at y = 0 and paint the leftover bottom strip themselves.
// ---------------------------------------------------------------------------

bool      gfx_begin();          // allocates; false means out of memory
bool      gfx_ok();
M5Canvas& gfx_canvas();

// Pushes the canvas and accumulates push timing.
void gfx_push(int16_t x, int16_t y);

// ---------------------------------------------------------------------------
// Frame instrumentation
//
// This exists because the whole animation budget hinges on one question: is
// the SPI push the bottleneck, or is canvas drawing? If the push dominates,
// every particle and trail below is effectively free, because the fixed cost
// is already being paid each frame. Guessing at that is how you either
// over-engineer or ship something that stutters. Measure it.
// ---------------------------------------------------------------------------
void gfx_perf_frame(uint32_t renderUs);   // total us spent in game_render()
void gfx_perf_report();                   // prints every PERF_REPORT_MS
