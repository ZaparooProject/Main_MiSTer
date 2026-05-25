#pragma once

#include <stdint.h>

#define ALT_LAUNCHER_MENUSUB     31

void alt_launcher_init(bool native_crt);
void alt_launcher_poll(void);
void alt_launcher_shutdown(void);
void alt_launcher_toggle_crt(void);
void alt_launcher_prepare_for_script(void);
void alt_launcher_resume_after_script(void);
bool alt_launcher_native_crt(void);
bool alt_launcher_active(void);
bool alt_launcher_configured(void);

// Display centering: signed offsets clamped to -8..+7. Setters update the
// in-memory cache, persist to the config dir, and push to the FPGA via
// user_io_status_set so the change takes effect immediately.
int8_t alt_launcher_h_offset(void);
int8_t alt_launcher_v_offset(void);
void alt_launcher_set_h_offset(int8_t v);
void alt_launcher_set_v_offset(int8_t v);

void alt_launcher_cfg_apply(void);
uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button);

bool zaparoo_is_native_core(void);
void zaparoo_alt_launcher_init_for_core(void);
void zaparoo_alt_launcher_init_for_menu(void);
