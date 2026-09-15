#pragma once
#include <Arduino.h>
#include "config.h"

// Fills dst[BRICK_COUNT] with the layout for the given level.
// Levels are 1-based and wrap once you pass LEVEL_COUNT, gaining an extra
// hit point on the upper rows each time around.
void levels_load(uint8_t level, uint8_t* dst);

// Number of bricks in dst that still need destroying (solids excluded).
uint16_t levels_destructible_remaining(const uint8_t* src);
