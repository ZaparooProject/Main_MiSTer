#pragma once

#include <stdint.h>

// Pulled in here so upstream files already including alt_launcher.h reach the
// kiosk predicate and the command dispatcher without a second include.
#include "command.h"
#include "kiosk.h"
#include "save.h"

#define ALT_LAUNCHER_MENUSUB     31

void alt_launcher_init(bool native_crt);
void alt_launcher_poll(void);
void alt_launcher_shutdown(void);
void alt_launcher_prepare_for_script(void);
void alt_launcher_resume_after_script(void);
bool alt_launcher_command(const char *cmd);
bool alt_launcher_native_crt(void);
bool alt_launcher_active(void);
// True from the moment a launcher start is queued (or a respawn is pending)
// until the child exits: the OSD must not auto-open over that window.
bool alt_launcher_owns_screen(void);
bool alt_launcher_console_lease_active(void);
// The frontend should run and own the screen: installed, enabled, not escaped.
bool alt_launcher_configured(void);
// The binary exists on disk. Ignores the enable setting and the escape flag,
// so OSD surfaces that can re-enable the frontend gate on this, never on
// alt_launcher_configured().
bool alt_launcher_installed(void);
// The persisted enable setting alone. Unlike alt_launcher_configured() it does
// not go false after a clean frontend exit, so menu rows read from it.
bool alt_launcher_enabled(void);
// Persists the enable setting and applies it live: tears a running frontend
// down on disable, spawns one on enable when the launcher core is up.
void alt_launcher_set_enabled(bool enabled);
void alt_launcher_installed_refresh(void);
// Returns the persisted native CRT enable state used by launcher restarts.
bool alt_launcher_native_crt_persisted(void);
// Flips the persisted native CRT state and respawns the launcher to apply it.
void alt_launcher_toggle_native_crt(void);
// Persisted video standard (state-file byte 1, see crt_settings.h).
uint8_t alt_launcher_native_crt_mode(void);
// Persists a new standard (state file + frontend.toml) and respawns a CRT launcher.
void alt_launcher_set_native_crt_mode(uint8_t mode);
// Restarts the launcher under the persisted CRT setting.
void alt_launcher_respawn(void);
bool alt_launcher_scheduler_sleep_enabled(void);
// Preserves HDMI launcher fb0 across queued startup and live mode changes.
bool alt_launcher_handle_video_fb_config(void);

void alt_launcher_cfg_apply(void);
uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button);

// Input hooks for input.cpp. While the launcher owns the screen its Qt frontend
// reads keyboards directly, so we grab them (kbd_grab) and bridge keys via uinput
// (kbd_to_frontend) to avoid double input; MENU/F12 and OSD-open keys are excluded.
int alt_launcher_kbd_grab(int fd);
bool alt_launcher_kbd_to_frontend(uint16_t code);

bool zaparoo_is_native_core(void);
void zaparoo_alt_launcher_init_for_core(void);
void zaparoo_alt_launcher_init_for_menu(void);
void zaparoo_alt_launcher_start_for_menu(void);
