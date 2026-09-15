#pragma once
#include <Arduino.h>

// Thin wrapper over M5.BtnA/B/C.
//
// Menus want edge events (one action per press).
// Paddle movement wants level state (continuous while held).
// Mixing the two up is what makes button-driven paddles feel terrible.

void input_update();          // call once per loop, after M5.update()

bool btnA_pressed();          // edge: went down this frame
bool btnB_pressed();
bool btnC_pressed();

bool btnA_held();             // level: is down right now
bool btnB_held();
bool btnC_held();

// True once per hold, the moment BtnB crosses LONG_PRESS_MS. Needed because the
// laser puts three jobs on BtnB, so pause moves to a long press while it lasts.
bool btnB_long_press();

bool any_pressed();
