#pragma once

#include <stdbool.h>
#include <stdint.h>

// OSD fallback for the frontend's CRT settings: the "Frontend" page reached
// from System Settings, and its "Position" sub-page. Renderers write the OSD
// directly (the pages fit in 16 rows); menu.cpp owns the state machine.

// Rows: 0 CRT mode, 1 Video standard (CRT on), 2 Screen position (CRT on), 3 exit.
void frontend_page_render(int menusub, uint64_t *menumask);
// Select on the highlighted row: 0 redraw, 1 enter Position, 2 leave the page.
int frontend_page_select(int menusub);

// Rows: 0 H offset, 1 V offset, 2 back. enter() snapshots the persisted trims
// and, with no frontend running, publishes an alignment pattern; leave()
// persists a change, restarts a running frontend so it picks the new values
// up, and withdraws the pattern.
void position_page_enter(void);
void position_page_render(int menusub, uint64_t *menumask);
void position_page_adjust(int menusub, int dir);
bool position_page_is_exit(int menusub);
void position_page_leave(void);
