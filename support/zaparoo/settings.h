#pragma once

#include <stdint.h>

// config/zaparoo_settings.bin. Loaded into a zeroed buffer, so an absent or
// short file reads as all-zero: every field encodes today's behavior as 0.
//
//   byte 0: frontend disabled  (0 = frontend runs)
//   byte 1: kiosk enabled      (0 = kiosk off)
//   byte 2: save on core exit  (0 = off)
//   byte 3: physical-disc autorun enabled (0 = off)
//   byte 4+: reserved, kept intact by the read-modify-write setters
#define ZAPAROO_SETTINGS_SIZE 16

bool zaparoo_settings_frontend_enabled(void);
bool zaparoo_settings_set_frontend_enabled(bool enabled);

bool zaparoo_settings_kiosk_active(void);
bool zaparoo_settings_set_kiosk(bool on);

bool zaparoo_settings_save_on_exit(void);
bool zaparoo_settings_set_save_on_exit(bool on);

bool zaparoo_settings_cd_autorun(void);
bool zaparoo_settings_set_cd_autorun(bool on);

void zaparoo_settings_invalidate(void);
