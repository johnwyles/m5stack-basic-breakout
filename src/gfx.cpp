#include "gfx.h"
#include "config.h"

static M5Canvas s_canvas(&M5.Display);
static bool     s_ok = false;

// --- perf accumulators ---
static uint32_t s_frames    = 0;
static uint32_t s_renderUs  = 0;
static uint32_t s_pushUs    = 0;
static uint32_t s_pushCount = 0;
static uint32_t s_lastReport = 0;

bool gfx_begin() {
    s_canvas.setColorDepth(8);
    s_ok = (s_canvas.createSprite(PLAY_W, PLAY_H) != nullptr);

    if (s_ok) {
        Serial.printf("[gfx] canvas %dx%d @8bpp ok (%u bytes), free heap %lu\n",
                      PLAY_W, PLAY_H, (unsigned)(PLAY_W * PLAY_H),
                      (unsigned long)ESP.getFreeHeap());
    } else {
        Serial.println("[gfx] FATAL: canvas allocation failed");
    }
    s_lastReport = millis();
    return s_ok;
}

bool      gfx_ok()     { return s_ok; }
M5Canvas& gfx_canvas() { return s_canvas; }

void gfx_push(int16_t x, int16_t y) {
    if (!s_ok) return;
    uint32_t t0 = micros();
    s_canvas.pushSprite(x, y);
    s_pushUs += (micros() - t0);
    ++s_pushCount;
}

void gfx_perf_frame(uint32_t renderUs) {
    s_renderUs += renderUs;
    ++s_frames;
}

void gfx_perf_report() {
    uint32_t now = millis();
    if (now - s_lastReport < PERF_REPORT_MS) return;

    if (s_frames) {
        uint32_t elapsed = now - s_lastReport;
        float fps      = (float)s_frames * 1000.0f / (float)elapsed;
        float renderMs = (float)s_renderUs / (float)s_frames / 1000.0f;
        float pushMs   = s_pushCount ? (float)s_pushUs / (float)s_pushCount / 1000.0f : 0.0f;

        // drawMs is what the animation work actually costs. pushMs is the fixed
        // SPI toll you pay whether you animate or not.
        Serial.printf("[perf] %.1f fps | render %.2f ms | push %.2f ms | draw %.2f ms | heap %lu\n",
                      fps, renderMs, pushMs, renderMs - pushMs,
                      (unsigned long)ESP.getFreeHeap());
    }

    s_frames = s_renderUs = s_pushUs = s_pushCount = 0;
    s_lastReport = now;
}
