#pragma once

#include <stdint.h>

#define ALT_LAUNCHER_MENUSUB 31

void alt_launcher_init(void);
void alt_launcher_poll(void);
void alt_launcher_shutdown(void);
bool alt_launcher_active(void);
bool alt_launcher_configured(void);

void alt_launcher_cfg_defaults(void);
uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button);
