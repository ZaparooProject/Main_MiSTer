#pragma once

#include <stdbool.h>

// zaparoo_cheat on|off|toggle <name|index>, zaparoo_cheat clear|list.
//
// Cheats reach the core through cheats_send(), which is user_io_set_index(255)
// plus a plain download. No OSD, no OSD_STATUS, no menu state, so this works
// under kiosk for the same reason the direct save row does: Main drives the
// transfer itself.
//
// cmd points at the argument list, past "zaparoo_cheat ".
void zaparoo_cheat_command(const char *cmd);
