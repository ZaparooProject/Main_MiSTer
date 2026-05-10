#pragma once

#include <stdbool.h>
#include <stdint.h>

// Right-side companion to the trimmed System Settings menu. Hosts the
// display centering controls (H/V offset) and the CRT mode toggle that
// used to live next to Reboot in alt_launcher_render_system_menu.
//
// menusub layout: 0 = H offset, 1 = V offset, 2 = CRT mode, 3 = Exit.

void display_menu_render(int menusub, uint64_t *menumask);

// Returns true if the press was consumed (re-render required without
// dispatching to MENU_NONE1). Caller checks this before exiting.
bool display_menu_handle_select(int menusub);

// Adjust the highlighted offset row by `dir` (-1 / +1). No-op when the
// current row is not an offset row.
void display_menu_adjust(int menusub, int dir);
