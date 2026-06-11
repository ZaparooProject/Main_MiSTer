#pragma once

#include <stdint.h>

#define ALT_LAUNCHER_MENUSUB     31

void alt_launcher_init(bool native_crt);
void alt_launcher_poll(void);
void alt_launcher_shutdown(void);
void alt_launcher_prepare_for_script(void);
void alt_launcher_resume_after_script(void);
bool alt_launcher_native_crt(void);
bool alt_launcher_active(void);
bool alt_launcher_configured(void);
bool alt_launcher_native_crt_persisted(void);
void alt_launcher_toggle_native_crt(void);

void alt_launcher_cfg_apply(void);
uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button);

bool zaparoo_is_native_core(void);
void zaparoo_alt_launcher_init_for_core(void);
void zaparoo_alt_launcher_init_for_menu(void);
