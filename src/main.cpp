#include <M5Unified.h>
#include "config.h"
#include "game.h"
#include "input.h"
#include "audio.h"
#include "storage.h"
#include "gfx.h"

static uint32_t s_lastMs      = 0;
static uint32_t s_accumulator = 0;

void setup() {
    // --- 1. hardware -------------------------------------------------------
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.clear_display = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);              // landscape, 320x240
    M5.Display.setBrightness(120);
    M5.Display.fillScreen(COL_BG);

    // --- 2. serial ---------------------------------------------------------
    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("[boot] M5Stack Basic Breakout");
    Serial.printf("[boot] built %s %s\n", __DATE__, __TIME__);

    // --- 3. storage probe (SD, then LittleFS, then none) -------------------
    storage_begin();

    // --- 4/5/6. high score, settings, audio --------------------------------
    // game_begin() loads the high score; settings default in game.cpp.
    audio_begin();
    audio_set_enabled(g_settings.sound);

    // --- 7/8. splash, then menu -------------------------------------------
    game_begin();
    sfx_startup();

    s_lastMs      = millis();
    s_accumulator = 0;
}

void loop() {
    M5.update();
    input_update();

    game_handle_input();

    // Fixed timestep. Rendering stutter changes how smooth the game looks,
    // never how fast it plays.
    uint32_t now   = millis();
    uint32_t delta = now - s_lastMs;
    s_lastMs = now;
    s_accumulator += delta;

    uint32_t steps = 0;
    while (s_accumulator >= TICK_MS && steps < MAX_CATCHUP_TICKS) {
        game_tick();
        s_accumulator -= TICK_MS;
        ++steps;
    }
    // If we fell badly behind, drop the backlog instead of spiralling.
    if (s_accumulator > TICK_MS * MAX_CATCHUP_TICKS) s_accumulator = 0;

    // Timed so the [perf] line can separate canvas drawing from the SPI push.
    // That split is the whole animation budget: if the push dominates, every
    // particle and trail is effectively free because the fixed cost is already
    // being paid. Watch it at 115200 before adding more effects.
    uint32_t t0 = micros();
    game_render();
    gfx_perf_frame(micros() - t0);
    gfx_perf_report();

    // No delay() here: it would fight the accumulator above.
}
