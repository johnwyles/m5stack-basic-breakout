#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Particles, floating text, and screen shake.
//
// All three exist because the playfield is a full back buffer that gets
// refilled and pushed every frame. With dirty-rect rendering each of these
// would need erase bookkeeping and would smear the moment two of them
// overlapped. With a canvas they are just draw calls, and the cost is dwarfed
// by the SPI push that is being paid anyway. Run the [perf] line in the serial
// log to confirm that on your board before adding more.
// ---------------------------------------------------------------------------

void fx_reset();

// Spread `count` particles from a point. `spread` is the initial speed in
// px/tick; they take gravity and drag from there.
void fx_burst(float x, float y, uint8_t count, uint16_t col, float spread);

// Floating text that rises and fades. Silently dropped when the pool is full,
// which is the right call: a missing +40 during a multiball frenzy is invisible,
// and stalling the frame to make room for it would not be.
void fx_popup(float x, float y, const char* text, uint16_t col);
void fx_popup_points(float x, float y, uint32_t pts, uint16_t col);

// Amplitude is clamped to FX_SHAKE_CAP. Calling again while a shake is running
// takes the larger of the two rather than stacking.
void fx_shake(uint8_t amplitude, uint8_t ticks);

// Transparent text with a one-pixel drop shadow.
//
// LovyanGFX decides transparency by comparing the two text colours: the
// single-argument setTextColor() sets fore and back to the same value, which
// makes `fillbg` false in the glyph blitter and leaves the background alone.
// The two-argument form is what was painting a black box behind every popup.
//
// Transparent text over particles and bricks can be hard to read, so this draws
// the string once offset in near-black and once on top in its real colour. Two
// draws rather than the five a full outline would need, and it holds up on any
// background.
void fx_shadow_string(M5Canvas& c, const char* text, int16_t x, int16_t y, uint16_t col);

void fx_update();                       // one 60Hz step
void fx_draw(M5Canvas& c);              // particles then popups, in that order

// Horizontal is free; vertical only ever goes down, because a negative dy
// would push the canvas over the HUD and force a full status-bar repaint every
// shaken frame. A one-directional bounce is indistinguishable at this size.
void fx_shake_offset(int16_t& dx, int16_t& dy);

bool fx_busy();                         // anything still alive
