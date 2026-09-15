#include "input.h"
#include "config.h"
#include <M5Unified.h>

struct BtnState { bool pressed; bool held; };
static BtnState s_a, s_b, s_c;

void input_update() {
    // M5Unified already debounces; wasPressed() is the clean falling edge.
    s_a.pressed = M5.BtnA.wasPressed();
    s_b.pressed = M5.BtnB.wasPressed();
    s_c.pressed = M5.BtnC.wasPressed();

    s_a.held = M5.BtnA.isPressed();
    s_b.held = M5.BtnB.isPressed();
    s_c.held = M5.BtnC.isPressed();
}

bool btnA_pressed() { return s_a.pressed; }
bool btnB_pressed() { return s_b.pressed; }
bool btnC_pressed() { return s_c.pressed; }

bool btnA_held() { return s_a.held; }
bool btnB_held() { return s_b.held; }
bool btnC_held() { return s_c.held; }

bool any_pressed() { return s_a.pressed || s_b.pressed || s_c.pressed; }

bool btnB_long_press() {
    static bool fired = false;

    if (!M5.BtnB.isPressed()) { fired = false; return false; }
    if (fired) return false;

    if (M5.BtnB.pressedFor(LONG_PRESS_MS)) { fired = true; return true; }
    return false;
}
