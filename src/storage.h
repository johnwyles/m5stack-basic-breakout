#pragma once
#include <Arduino.h>

enum StorageBackend : uint8_t {
    STORAGE_NONE = 0,   // nothing mounted; high score is RAM-only this session
    STORAGE_SD,
    STORAGE_FLASH
};

// Probe SD first, then LittleFS. Safe to call once, from setup().
void           storage_begin();
StorageBackend storage_backend();
const char*    storage_backend_name();
bool           storage_available();

// Returns 0 for a missing, truncated or corrupt record. Never throws an error
// at the player; a missing file on first boot is normal.
uint32_t storage_load_high_score();

// Returns false if the write failed or there is no backend.
bool storage_save_high_score(uint32_t score);
