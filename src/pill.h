#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "entities.h"

// ---------------------------------------------------------------------------
// One pill, drawn one way.
//
// This is a template rather than a function because the playfield draws into an
// M5Canvas and the key screen draws onto M5.Display, and both satisfy the same
// small interface. The point is that there is exactly ONE pill-drawing routine
// in the project. A hand-copied version in the legend would look identical on
// the day it was written and silently drift the first time a colour changed,
// which is the failure mode that makes a key screen worse than no key screen.
//
// Shape carries valence, not just colour:
//   positive -> rounded capsule
//   negative -> square body with its corners bitten out
//
// That matters because two reds are both "bad" and the player should be able to
// tell good from bad on a dim screen in a bright room without reading a letter.
// ---------------------------------------------------------------------------
template <typename GFX>
void pill_draw(GFX& g, int16_t x, int16_t y, int16_t w, int16_t h,
               DropType t, bool showLetter = true) {
    const DropSpec& spec = DROPS[t];

    if (!spec.negative) {
        int16_t r = (h < 10) ? 3 : 4;
        g.fillRoundRect(x, y, w, h, r, spec.color);
        g.drawRoundRect(x, y, w, h, r, dim565(spec.color, 1, 3));
    } else {
        g.fillRect(x, y, w, h, spec.color);

        // Bite the corners out so the silhouette alone reads as hostile.
        const int16_t n = (h < 12) ? 2 : 3;
        g.fillRect(x,             y,             n, n, COL_BG);
        g.fillRect(x + w - n,     y,             n, n, COL_BG);
        g.fillRect(x,             y + h - n,     n, n, COL_BG);
        g.fillRect(x + w - n,     y + h - n,     n, n, COL_BG);

        g.drawRect(x + 1, y + 1, w - 2, h - 2, dim565(spec.color, 1, 3));
    }

    if (!showLetter) return;

    g.setFont(&fonts::Font0);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(COL_BG, spec.color);
    char b[2] = { spec.letter, 0 };
    g.drawString(b, x + w / 2, y + h / 2);
}
