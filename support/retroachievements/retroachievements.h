#pragma once

#include <stdint.h>

// Hook layer between upstream files and the vendored RA code in this
// directory. Every hook is a no-op unless the running core is an RA core.

void ra_poll(void);
void ra_game_loaded(const char *path, uint32_t crc);
void ra_core_reset(void);
// After a core OSD option pulse: bit/ex from the status mask, conf is the
// option's conf-string entry.
void ra_status_option_set(int bit, int ex, const char *conf);
void ra_save_io(void);
void ra_osd_enable(unsigned char mode);
bool ra_cheats_blocked(bool notify);

// Menu+Y in-game shortcut to the achievement list.
int ra_osd_event_filter(int dev, int ev_type, int code, int value, uint32_t y_code, int osd_event, int mapping);

// Achievements row in the core's main OSD page.
bool ra_menu_add_row(int page, int row);
bool ra_menu_row_selected(uint32_t menusub, int select);

// Achievement list and detail views. open() shows the OSD and remembers
// where to return; the caller then sets menusub = 0 and enters the list state.
bool ra_list_open(uint32_t ret_state, uint32_t ret_sub);
void ra_list_render(void);
enum { RA_LIST_NONE, RA_LIST_REDRAW, RA_LIST_DETAIL, RA_LIST_CLOSE };
int ra_list_input(int back, int select, int up, int down, uint32_t key, uint32_t *ret_state, uint32_t *ret_sub);
void ra_detail_render(void);

void ra_n64_set_rom_md5(const char *md5_hex);

bool ra_skip_main_restart(const char *exe);

// CA bundle for RA HTTPS requests, or NULL for curl's default store.
const char *ra_ca_bundle_path(void);
