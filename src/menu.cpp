#include "menu.h"
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "entities.h"
#include "game.h"
#include "input.h"
#include "audio.h"
#include "storage.h"
#include "gfx.h"
#include "pill.h"

// ---------------------------------------------------------------------------
// Menus draw into the shared canvas and push at (0,0), which covers screen rows
// 0..PLAY_H-1. Gameplay pushes the same canvas at (0, PLAY_Y0) so the HUD
// survives above it. The 20px strip below PLAY_H is painted once on entry.
//
// Doing it this way is what makes attract mode possible at all: a moving
// background needs a back buffer, and rather than allocate a second one we
// reuse the one already sitting there.
// ---------------------------------------------------------------------------
static bool s_bottomPainted = false;

static void paintBottomStrip() {
    if (s_bottomPainted) return;
    M5.Display.fillRect(0, PLAY_H, SCREEN_W, SCREEN_H - PLAY_H, COL_BG);
    s_bottomPainted = true;
}

static void menuPush() {
    paintBottomStrip();
    gfx_push(0, 0);
}

// Call whenever something else has taken over the panel.
static void invalidateStrip() { s_bottomPainted = false; }

// ---------------------------------------------------------------------------
// Attract mode
//
// Demo balls bouncing behind the title. They are drawn BEFORE the menu text, so
// the text glyph boxes paint over them and the balls read as passing behind the
// words. That layering is free here and would have needed per-glyph masking
// under dirty-rect rendering.
// ---------------------------------------------------------------------------
struct AttractBall { float x, y, vx, vy; uint16_t col; };
static AttractBall s_attract[ATTRACT_BALLS];
static bool        s_attractReady = false;
static uint32_t    s_tick = 0;

static void attractInit() {
    for (uint8_t i = 0; i < ATTRACT_BALLS; ++i) {
        AttractBall& b = s_attract[i];
        b.x  = (float)random(20, PLAY_W - 20);
        b.y  = (float)random(20, PLAY_H - 20);
        float ang = ((float)random(1000) / 1000.0f) * 2.0f * PI;
        float sp  = 0.7f + (float)random(70) / 100.0f;
        b.vx = cosf(ang) * sp;
        b.vy = sinf(ang) * sp;
        b.col = ROW_COLORS[i % BRICK_ROWS];
    }
    s_attractReady = true;
}

static void attractTick() {
    if (!s_attractReady) attractInit();

    for (uint8_t i = 0; i < ATTRACT_BALLS; ++i) {
        AttractBall& b = s_attract[i];
        b.x += b.vx;
        b.y += b.vy;
        if (b.x < 0)                 { b.x = 0;                 b.vx = -b.vx; }
        if (b.x > PLAY_W - BALL_SIZE_BASE) { b.x = PLAY_W - BALL_SIZE_BASE; b.vx = -b.vx; }
        if (b.y < 0)                 { b.y = 0;                 b.vy = -b.vy; }
        if (b.y > PLAY_H - BALL_SIZE_BASE) { b.y = PLAY_H - BALL_SIZE_BASE; b.vy = -b.vy; }
    }
}

static void attractDraw(M5Canvas& c) {
    if (!s_attractReady) return;
    for (uint8_t i = 0; i < ATTRACT_BALLS; ++i) {
        const AttractBall& b = s_attract[i];
        // Dimmed hard: this is wallpaper, and anything brighter competes with
        // the menu text for attention.
        c.fillRect((int16_t)b.x, (int16_t)b.y, BALL_SIZE_BASE, BALL_SIZE_BASE,
                   dim565(b.col, 1, 4));
    }
}

// Breathing highlight on the selected row. A static cursor on a static screen
// looks like the firmware has hung.
static uint16_t cursorColor() {
    float  s = (sinf((float)s_tick * 0.13f) + 1.0f) * 0.5f;   // 0..1
    uint16_t num = (uint16_t)(10 + s * 6);                     // 10..16
    return dim565(COL_ACCENT, num, 16);
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------
static char     s_toast[32] = {0};
static uint32_t s_toastUntil = 0;

void menu_toast(const char* msg, uint32_t ms) {
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = 0;
    s_toastUntil = millis() + ms;
}

static void toastDraw(M5Canvas& c) {
    if (s_toastUntil == 0 || millis() >= s_toastUntil) { s_toastUntil = 0; return; }

    const int16_t h = 40, y = (PLAY_H - h) / 2;
    c.fillRect(20, y, PLAY_W - 40, h, COL_BG);
    c.drawRect(20, y, PLAY_W - 40, h, COL_LIFE);
    c.setFont(&fonts::Font2);
    c.setTextDatum(textdatum_t::middle_center);
    c.setTextColor(COL_LIFE, COL_BG);
    c.drawString(s_toast, PLAY_W / 2, y + h / 2);
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------
enum MenuItem : uint8_t {
    MI_START = 0,
    MI_DIFFICULTY,
    MI_SOUND,
    MI_KEY,
    MI_RESET,
    MI_COUNT
};

static uint8_t s_cursor   = MI_START;
static bool    s_confirm  = false;    // reset confirmation screen showing
static uint8_t s_confirmC = 0;        // 0 == No (default), 1 == Yes

void menu_reset() {
    s_cursor   = MI_START;
    s_confirm  = false;
    s_confirmC = 0;
    invalidateStrip();
}

static void cursorMove(int8_t delta, uint8_t count, uint8_t& cur) {
    cur = (uint8_t)((cur + count + delta) % count);
    sfx_menu_move();
}

void menu_tick() {
    ++s_tick;
    attractTick();
}

MenuAction menu_update() {
    // --- confirm sub-screen ---
    if (s_confirm) {
        if (btnA_pressed()) cursorMove(-1, 2, s_confirmC);
        if (btnC_pressed()) cursorMove(+1, 2, s_confirmC);
        if (btnB_pressed()) {
            sfx_menu_select();
            bool yes = (s_confirmC == 1);
            s_confirm  = false;
            s_confirmC = 0;
            return yes ? MA_RESET_HIGH : MA_NONE;
        }
        return MA_NONE;
    }

    // --- main list ---
    if (btnA_pressed()) cursorMove(-1, MI_COUNT, s_cursor);
    if (btnC_pressed()) cursorMove(+1, MI_COUNT, s_cursor);

    if (btnB_pressed()) {
        switch (s_cursor) {
            case MI_START:
                sfx_menu_select();
                return MA_START;

            case MI_DIFFICULTY:
                g_settings.diff = (Difficulty)((g_settings.diff + 1) % DIFF_COUNT);
                sfx_menu_select();
                break;

            case MI_SOUND:
                g_settings.sound = !g_settings.sound;
                audio_set_enabled(g_settings.sound);
                sfx_menu_select();          // silent if we just turned it off
                break;

            case MI_KEY:
                sfx_menu_select();
                return MA_SHOW_KEY;

            case MI_RESET:
                if (storage_available()) {
                    s_confirm  = true;
                    s_confirmC = 0;         // default to No, always
                    sfx_menu_select();
                } else {
                    sfx_powerup_bad();
                }
                break;
        }
    }
    return MA_NONE;
}

static void drawRow(M5Canvas& c, int16_t y, const char* label, const char* value,
                    bool selected, bool enabled) {
    uint16_t fg = enabled ? (selected ? cursorColor() : COL_TEXT) : COL_DIM;

    // No fillRect here any more: the canvas is cleared every frame, and wiping
    // a full-width band would erase the attract balls behind the row.
    c.setTextColor(fg, COL_BG);
    c.setTextDatum(textdatum_t::middle_left);
    c.drawString(selected ? ">" : " ", 26, y);
    c.drawString(label, 46, y);
    if (value) {
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(value, PLAY_W - 26, y);
    }
}

void menu_draw() {
    if (!gfx_ok()) return;
    M5Canvas& c = gfx_canvas();

    c.fillSprite(COL_BG);
    c.setFont(&fonts::Font2);

    if (s_confirm) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Erase high score %05lu?",
                 (unsigned long)game_high_score());

        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(COL_WARN, COL_BG);
        c.drawString("RESET HIGH SCORE", PLAY_W / 2, 40);
        c.setTextColor(COL_TEXT, COL_BG);
        c.drawString(buf, PLAY_W / 2, 74);

        drawRow(c, 124, "No",  nullptr, s_confirmC == 0, true);
        drawRow(c, 152, "Yes", nullptr, s_confirmC == 1, true);

        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(COL_DIM, COL_BG);
        c.drawString("A up    B select    C down", PLAY_W / 2, 200);
        menuPush();
        return;
    }

    attractDraw(c);

    c.setTextDatum(textdatum_t::middle_center);
    c.setTextColor(COL_ACCENT, COL_BG);
    c.drawString("BREAKOUT", PLAY_W / 2, 16);

    char hi[24];
    snprintf(hi, sizeof(hi), "HIGH  %05lu", (unsigned long)game_high_score());
    c.setTextColor(COL_DIM, COL_BG);
    c.drawString(hi, PLAY_W / 2, 38);

    drawRow(c,  70, "Start Game", nullptr,                          s_cursor == MI_START,      true);
    drawRow(c,  94, "Difficulty", DIFFICULTY[g_settings.diff].name, s_cursor == MI_DIFFICULTY, true);
    drawRow(c, 118, "Sound",      g_settings.sound ? "On" : "Off",  s_cursor == MI_SOUND,      true);
    drawRow(c, 142, "Powerup Key", nullptr,                         s_cursor == MI_KEY,        true);

    c.drawFastHLine(46, 158, PLAY_W - 92, COL_DIM);

    drawRow(c, 174, "Reset High Score", nullptr, s_cursor == MI_RESET, storage_available());

    c.setTextDatum(textdatum_t::middle_center);
    if (storage_available()) {
        char sb[40];
        snprintf(sb, sizeof(sb), "saving to %s", storage_backend_name());
        c.setTextColor(COL_DIM, COL_BG);
        c.drawString(sb, PLAY_W / 2, 196);
    } else {
        c.setTextColor(COL_WARN, COL_BG);
        c.drawString("no storage - scores not saved", PLAY_W / 2, 196);
    }
    c.setTextColor(COL_DIM, COL_BG);
    c.drawString("A up    B select    C down", PLAY_W / 2, 212);

    toastDraw(c);
    menuPush();
}

// ---------------------------------------------------------------------------
// Powerup key
//
// Ten entries will not fit legibly on 320x240 with readable descriptions, so it
// pages five at a time. A and C page, B backs out. The swatches come from
// pill_draw(), the same routine the playfield uses, so what you see here is
// literally what falls at you.
// ---------------------------------------------------------------------------
static uint8_t s_keyPage = 0;

// The key pages off the positive/negative boundary in the DROPS table so a page
// never straddles the two groups. Eighteen entries in two labelled sections
// reads far smaller than eighteen in one undifferentiated list, which is most
// of why the table could stay this size at all.
static const uint8_t KEY_ROWS = 5;

static const uint8_t POS_COUNT  = PU_NEG_FIRST;
static const uint8_t NEG_COUNT  = PU_COUNT - PU_NEG_FIRST;
static const uint8_t POS_PAGES  = (POS_COUNT + KEY_ROWS - 1) / KEY_ROWS;
static const uint8_t NEG_PAGES  = (NEG_COUNT + KEY_ROWS - 1) / KEY_ROWS;
static const uint8_t MARKS_PAGE = POS_PAGES + NEG_PAGES;     // last page
static const uint8_t KEY_PAGES  = MARKS_PAGE + 1;

void key_reset() { s_keyPage = 0; invalidateStrip(); }
void key_tick()  { ++s_tick; }

bool key_update() {
    if (btnA_pressed()) {
        s_keyPage = (uint8_t)((s_keyPage + KEY_PAGES - 1) % KEY_PAGES);
        sfx_menu_move();
    }
    if (btnC_pressed()) {
        s_keyPage = (uint8_t)((s_keyPage + 1) % KEY_PAGES);
        sfx_menu_move();
    }
    if (btnB_pressed()) {
        sfx_menu_select();
        return true;
    }
    return false;
}

// The two dashed lines on the playfield are the only things in the game that
// mean something without being a pill, so they get their own page. A marking
// you have to guess at is worse than no marking.
static void drawFieldMarks(M5Canvas& c) {
    c.setFont(&fonts::Font2);
    c.setTextDatum(textdatum_t::middle_center);
    c.setTextColor(COL_ACCENT);
    c.drawString("FIELD MARKS", PLAY_W / 2, 14);

    // --- safety net ---
    int16_t y = 54;
    for (int16_t x = 16; x < 116; x += 10) c.fillRect(x, y, 5, 2, DROPS[PU_NET].color);
    c.setTextDatum(textdatum_t::middle_left);
    c.setTextColor(DROPS[PU_NET].color);
    c.drawString("SAFETY NET", 130, y);
    c.setFont(&fonts::Font0);
    c.setTextColor(COL_DIM);
    c.drawString("Catches one missed ball,", 130, y + 16);
    c.drawString("then it is gone. From the", 130, y + 26);
    c.drawString("= pill.", 130, y + 36);

    // --- crush line ---
    y = 140;
    c.setFont(&fonts::Font2);
    for (int16_t x = 16; x < 116; x += 10) c.fillRect(x, y, 5, 1, dim565(COL_WARN, 2, 3));
    c.setTextDatum(textdatum_t::middle_left);
    c.setTextColor(COL_WARN);
    c.drawString("CRUSH LINE", 130, y);
    c.setFont(&fonts::Font0);
    c.setTextColor(COL_DIM);
    c.drawString("Only appears once V has", 130, y + 16);
    c.drawString("pushed the bricks down.", 130, y + 26);
    c.drawString("Bricks touching it = over.", 130, y + 36);
}

void key_draw() {
    if (!gfx_ok()) return;
    M5Canvas& c = gfx_canvas();

    c.fillSprite(COL_BG);

    char pg[16];
    snprintf(pg, sizeof(pg), "%u / %u", (unsigned)(s_keyPage + 1), (unsigned)KEY_PAGES);
    c.setFont(&fonts::Font2);
    c.setTextColor(COL_DIM);
    c.setTextDatum(textdatum_t::middle_right);
    c.drawString(pg, PLAY_W - 10, 14);

    if (s_keyPage == MARKS_PAGE) {
        drawFieldMarks(c);
    } else {
        bool     neg   = (s_keyPage >= POS_PAGES);
        uint8_t  local = neg ? (uint8_t)(s_keyPage - POS_PAGES) : s_keyPage;
        uint8_t  first = (uint8_t)((neg ? PU_NEG_FIRST : 0) + local * KEY_ROWS);
        uint8_t  last  = neg ? (uint8_t)PU_COUNT : (uint8_t)PU_NEG_FIRST;

        c.setFont(&fonts::Font2);
        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(neg ? COL_WARN : COL_LIFE);
        c.drawString(neg ? "AVOID THESE" : "GOOD TO CATCH", PLAY_W / 2, 14);

        const int16_t y0 = 44, dy = 32;
        for (uint8_t i = 0; i < KEY_ROWS; ++i) {
            uint8_t idx = (uint8_t)(first + i);
            if (idx >= last) break;

            const DropSpec& spec = DROPS[idx];
            int16_t y = y0 + i * dy;

            pill_draw(c, 12, y - 8, 26, 16, (DropType)idx, true);

            c.setFont(&fonts::Font2);
            c.setTextDatum(textdatum_t::middle_left);
            c.setTextColor(spec.color);
            c.drawString(spec.name, 48, y);

            c.setFont(&fonts::Font0);
            c.setTextColor(COL_DIM);
            c.drawString(spec.desc, 150, y);
        }

        c.setFont(&fonts::Font0);
        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(COL_DIM);
        c.drawString("rounded = good      notched corners = avoid", PLAY_W / 2, 196);
    }

    c.setFont(&fonts::Font2);
    c.setTextDatum(textdatum_t::middle_center);
    c.setTextColor(COL_DIM);
    c.drawString("A / C page     B back", PLAY_W / 2, 212);

    menuPush();
}

// ---------------------------------------------------------------------------
// Pause menu
// ---------------------------------------------------------------------------
enum PauseItem : uint8_t { PI_RESUME = 0, PI_RESTART, PI_KEY, PI_QUIT, PI_COUNT };

static uint8_t s_pCursor = PI_RESUME;
static bool    s_pDirty  = true;

void pause_reset() { s_pCursor = PI_RESUME; s_pDirty = true; }

MenuAction pause_update() {
    if (btnA_pressed()) { cursorMove(-1, PI_COUNT, s_pCursor); s_pDirty = true; }
    if (btnC_pressed()) { cursorMove(+1, PI_COUNT, s_pCursor); s_pDirty = true; }

    if (btnB_pressed()) {
        sfx_menu_select();
        switch (s_pCursor) {
            case PI_RESUME:  return MA_RESUME;
            case PI_RESTART: return MA_RESTART;
            case PI_KEY:     return MA_SHOW_KEY;
            case PI_QUIT:    return MA_QUIT;
        }
    }
    return MA_NONE;
}

void pause_draw(bool force) {
    if (!force && !s_pDirty) return;
    s_pDirty = false;

    auto& d = M5.Display;
    d.setFont(&fonts::Font2);

    // Panel only, straight onto the display, so the frozen playfield stays
    // visible behind it. This one screen is deliberately NOT canvas-based:
    // the canvas is holding the paused playfield and we want to keep it.
    const int16_t px = 60, py = 54, pw = SCREEN_W - 120, ph = 132;
    d.fillRect(px, py, pw, ph, COL_BG);
    d.drawRect(px, py, pw, ph, COL_ACCENT);

    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(COL_ACCENT, COL_BG);
    d.drawString("PAUSED", SCREEN_W / 2, py + 18);

    const char* labels[PI_COUNT] = { "Resume", "Restart", "Powerup Key", "Quit to Menu" };
    for (uint8_t i = 0; i < PI_COUNT; ++i) {
        int16_t y = py + 46 + i * 22;
        d.setTextColor(i == s_pCursor ? COL_ACCENT : COL_TEXT, COL_BG);
        d.setTextDatum(textdatum_t::middle_center);
        char row[32];
        snprintf(row, sizeof(row), "%s%s", i == s_pCursor ? "> " : "  ", labels[i]);
        d.drawString(row, SCREEN_W / 2, y);
    }
}

// ---------------------------------------------------------------------------
// One-shot screens (direct to display)
// ---------------------------------------------------------------------------
void screen_splash(uint32_t highScore, const char* backend) {
    auto& d = M5.Display;
    invalidateStrip();
    d.fillScreen(COL_BG);
    d.setFont(&fonts::Font4);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(COL_ACCENT, COL_BG);
    d.drawString("BREAKOUT", SCREEN_W / 2, 92);

    d.setFont(&fonts::Font2);
    d.setTextColor(COL_TEXT, COL_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "HIGH  %05lu", (unsigned long)highScore);
    d.drawString(buf, SCREEN_W / 2, 132);

    d.setTextColor(COL_DIM, COL_BG);
    snprintf(buf, sizeof(buf), "storage: %s", backend);
    d.drawString(buf, SCREEN_W / 2, 160);
    d.drawString("press any button", SCREEN_W / 2, 200);
}

void screen_level_clear(uint8_t level, uint32_t score) {
    auto& d = M5.Display;
    const int16_t px = 50, py = 74, pw = SCREEN_W - 100, ph = 92;
    d.fillRect(px, py, pw, ph, COL_BG);
    d.drawRect(px, py, pw, ph, COL_LIFE);

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(COL_LIFE, COL_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "LEVEL %u CLEAR", (unsigned)level);
    d.drawString(buf, SCREEN_W / 2, py + 24);

    d.setTextColor(COL_TEXT, COL_BG);
    snprintf(buf, sizeof(buf), "Score  %05lu", (unsigned long)score);
    d.drawString(buf, SCREEN_W / 2, py + 48);

    d.setTextColor(COL_DIM, COL_BG);
    d.drawString("B to continue", SCREEN_W / 2, py + 72);
}

void screen_game_over(uint32_t score, uint32_t high, bool beat, uint8_t bestChain,
                      bool crushed) {
    auto& d = M5.Display;
    invalidateStrip();
    d.fillScreen(COL_BG);
    d.setFont(&fonts::Font4);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(COL_WARN, COL_BG);
    d.drawString(crushed ? "CRUSHED" : "GAME OVER", SCREEN_W / 2, 60);

    d.setFont(&fonts::Font2);
    char buf[40];
    if (crushed) {
        d.setTextColor(COL_DIM, COL_BG);
        d.drawString("the bricks reached the floor", SCREEN_W / 2, 86);
    }
    d.setTextColor(COL_TEXT, COL_BG);
    snprintf(buf, sizeof(buf), "Score  %05lu", (unsigned long)score);
    d.drawString(buf, SCREEN_W / 2, 106);

    snprintf(buf, sizeof(buf), "High   %05lu", (unsigned long)high);
    d.setTextColor(COL_ACCENT, COL_BG);
    d.drawString(buf, SCREEN_W / 2, 130);

    if (bestChain >= COMBO_POPUP_MIN) {
        snprintf(buf, sizeof(buf), "Best chain  %u", (unsigned)bestChain);
        d.setTextColor(COL_DIM, COL_BG);
        d.drawString(buf, SCREEN_W / 2, 154);
    }

    if (beat) {
        d.setTextColor(COL_LIFE, COL_BG);
        d.drawString("NEW HIGH SCORE", SCREEN_W / 2, 178);
        if (!storage_available()) {
            d.setTextColor(COL_WARN, COL_BG);
            d.drawString("(not saved - no storage)", SCREEN_W / 2, 196);
        }
    }

    d.setTextColor(COL_DIM, COL_BG);
    d.drawString("B for menu", SCREEN_W / 2, 214);
}
