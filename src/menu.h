#pragma once
#include <Arduino.h>

enum MenuAction : uint8_t {
    MA_NONE = 0,
    MA_START,          // main menu: Start Game
    MA_RESET_HIGH,     // main menu: reset confirmed with Yes
    MA_SHOW_KEY,       // either menu: open the powerup key
    MA_RESUME,         // pause menu: Resume
    MA_RESTART,        // pause menu: Restart
    MA_QUIT            // pause menu: Quit to Menu
};

// --- main menu -------------------------------------------------------------
// The main menu now renders into the shared canvas and repaints every frame,
// because the attract-mode balls behind it have to move. That costs exactly one
// canvas push per frame, which is the same toll gameplay already pays.
void       menu_reset();                 // cursor home, leave any confirm screen
MenuAction menu_update();                // consumes input, returns an action
void       menu_tick();                  // 60Hz: attract balls, cursor pulse
void       menu_draw();

// Brief centred message over the menu. Owned by the menu now rather than
// painted over it, since a full-frame redraw would otherwise wipe it.
void menu_toast(const char* msg, uint32_t ms);

// --- pause menu ------------------------------------------------------------
void       pause_reset();
MenuAction pause_update();
void       pause_draw(bool force);

// --- powerup key -----------------------------------------------------------
// Reachable from both menus. From the pause menu it matters most: that is when
// something just landed on your paddle and you want to know what it did.
void key_reset();
bool key_update();      // true once the player has backed out
void key_tick();
void key_draw();

// --- one-shot screens ------------------------------------------------------
void screen_splash(uint32_t highScore, const char* backend);
void screen_level_clear(uint8_t level, uint32_t score);
void screen_game_over(uint32_t score, uint32_t high, bool beat, uint8_t bestChain,
                      bool crushed);
