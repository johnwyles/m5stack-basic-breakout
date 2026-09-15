#include "storage.h"
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <SPI.h>

// M5Stack Basic: microSD chip select is GPIO4, sharing VSPI with the display.
static const int SD_CS_PIN = 4;

static const char*    REC_PATH    = "/breakout.dat";
static const uint32_t REC_MAGIC   = 0x4F4B5242UL;   // "BRKO"
static const uint16_t REC_VERSION = 1;

struct __attribute__((packed)) Record {
    uint32_t magic;
    uint16_t version;
    uint32_t score;
    uint16_t crc;       // CRC16-CCITT over the preceding 10 bytes
};

static StorageBackend s_backend = STORAGE_NONE;
static fs::FS*        s_fs      = nullptr;
static uint32_t       s_ramHigh = 0;    // used only when s_backend == STORAGE_NONE

// ---------------------------------------------------------------------------
static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
static bool trySD() {
    if (!SD.begin(SD_CS_PIN, SPI, 25000000)) return false;

    // Confirm the card is actually writable before committing to it.
    File f = SD.open("/.bo_probe", FILE_WRITE);
    if (!f) { SD.end(); return false; }
    f.write((const uint8_t*)"1", 1);
    f.close();
    SD.remove("/.bo_probe");

    s_fs = &SD;
    return true;
}

static bool tryFlash() {
    if (!LittleFS.begin(true)) return false;   // true == format on first failure
    s_fs = &LittleFS;
    return true;
}

void storage_begin() {
    if (trySD()) {
        s_backend = STORAGE_SD;
    } else if (tryFlash()) {
        s_backend = STORAGE_FLASH;
    } else {
        s_backend = STORAGE_NONE;
        s_fs      = nullptr;
    }
    Serial.printf("[storage] backend=%s\n", storage_backend_name());
}

StorageBackend storage_backend() { return s_backend; }
bool           storage_available() { return s_backend != STORAGE_NONE; }

const char* storage_backend_name() {
    switch (s_backend) {
        case STORAGE_SD:    return "SD";
        case STORAGE_FLASH: return "LittleFS";
        default:            return "none";
    }
}

// ---------------------------------------------------------------------------
uint32_t storage_load_high_score() {
    if (!s_fs) return s_ramHigh;

    File f = s_fs->open(REC_PATH, FILE_READ);
    if (!f) return 0;                       // first boot, entirely normal

    Record r;
    size_t n = f.read((uint8_t*)&r, sizeof(r));
    f.close();

    if (n != sizeof(r))          { Serial.println("[storage] short record"); return 0; }
    if (r.magic   != REC_MAGIC)  { Serial.println("[storage] bad magic");    return 0; }
    if (r.version != REC_VERSION){ Serial.println("[storage] bad version");  return 0; }

    uint16_t want = crc16((const uint8_t*)&r, sizeof(Record) - sizeof(uint16_t));
    if (want != r.crc)           { Serial.println("[storage] bad crc");      return 0; }

    return r.score;
}

bool storage_save_high_score(uint32_t score) {
    if (!s_fs) { s_ramHigh = score; return false; }

    Record r;
    r.magic   = REC_MAGIC;
    r.version = REC_VERSION;
    r.score   = score;
    r.crc     = crc16((const uint8_t*)&r, sizeof(Record) - sizeof(uint16_t));

    File f = s_fs->open(REC_PATH, FILE_WRITE);
    if (!f) { Serial.println("[storage] open for write failed"); return false; }

    size_t n = f.write((const uint8_t*)&r, sizeof(r));
    f.flush();
    f.close();

    if (n != sizeof(r)) { Serial.println("[storage] short write"); return false; }
    Serial.printf("[storage] saved high score %lu\n", (unsigned long)score);
    return true;
}
