#pragma once

#include <stdint.h>

#define ALT_LAUNCHER_MENUSUB 31

void alt_launcher_init(bool native_crt = false);
void alt_launcher_poll(void);
void alt_launcher_shutdown(void);
bool alt_launcher_active(void);
bool alt_launcher_configured(void);

void alt_launcher_cfg_defaults(void);
uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button);

bool zaparoo_is_native_core(void);
void zaparoo_alt_launcher_init_for_core(void);
