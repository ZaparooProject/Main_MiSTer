#pragma once

#include <stdbool.h>
#include <stdint.h>

// The "Zaparoo" page reached from System Settings, its "Position" sub-page and
// the kiosk confirmation. Renderers write the OSD directly (the pages fit in
// 16 rows); menu.cpp owns the state machine.

// Rows: 0 Frontend, 1 Kiosk mode, 2 Auto-save, 3 Auto-run discs,
// 4 CRT mode, 5 Video standard (CRT on), 6 Screen position (CRT on),
// 7 exit.
void frontend_page_render(int menusub, uint64_t *menumask);
// Select on the highlighted row: 0 redraw, 1 enter Position, 2 leave the page,
// 3 enter the kiosk confirmation, 4 enter the auto-save confirmation.
int frontend_page_select(int menusub);
// True for rows that open a sub-page, so Right enters them. Keeps menu.cpp
// from carrying a row index that has to move whenever this page changes.
bool frontend_page_row_has_submenu(int menusub);

// Kiosk confirmation. Rows: 0 No, 1 Yes. confirm() returns true only when
// kiosk was switched on and persisted; that closes the OSD, so the caller must
// not keep rendering after a true return. False (No, or the settings file
// could not be written) leaves everything as it was.
void kiosk_page_render(int menusub, uint64_t *menumask);
bool kiosk_page_confirm(int menusub);

// Auto-save confirmation. Rows: 0 No, 1 Yes. Only guards turning it on: the
// name promises protection against a power cut that it cannot give. confirm()
// returns true only when the setting was persisted.
void autosave_page_render(int menusub, uint64_t *menumask);
bool autosave_page_confirm(int menusub);

// Rows: 0 H offset, 1 V offset, 2 back. enter() snapshots the persisted trims
// and, with no frontend running, publishes an alignment pattern; leave()
// persists a change, restarts a running frontend so it picks the new values
// up, and withdraws the pattern.
void position_page_enter(void);
void position_page_render(int menusub, uint64_t *menumask);
void position_page_adjust(int menusub, int dir);
bool position_page_is_exit(int menusub);
void position_page_leave(void);
