#pragma once

#include <stdbool.h>
#include <stdint.h>

// Trimmed System Settings menu for alt-launcher mode. The frontend is
// the only "core" running on top of menu.rbf, so storage toggle and
// the bundled help PDF are irrelevant — this layout hides them and
// exposes Remap(0), Define joy(1), Scripts(2), Reboot(3), Exit(4).
// CRT mode lives on the Zaparoo Frontend's Video sub-page.
//
// All three helpers are no-ops / return safe defaults when
// alt_launcher_configured() is false, so menu.cpp's hook sites can
// be unconditional. Caller must have already set OsdSetSize(16);
// returns the row of the Reboot line so the cold-reboot animation
// in MENU_SYSTEM2 can target the right OSD slot.
int alt_launcher_render_system_menu(int menusub, uint64_t *menumask,
                                    int *reboot_req,
                                    long *sysinfo_timer);

// Translate a select press on the trimmed menu to its upstream
// MENU_SYSTEM2 switch case index. Returns -1 when the press is
// consumed inline (CRT toggle); the caller should re-render
// MENU_SYSTEM1 and skip the dispatch.
int alt_launcher_translate_system_select(int menusub);

// menu.cpp's hold-to-cold-reboot key-repeat gate hard-codes
// menusub==5 (the upstream Reboot row). The trimmed menu places
// Reboot at menusub 3, so menu.cpp ORs in this helper.
bool alt_launcher_system_holding_reboot(int menusub);
