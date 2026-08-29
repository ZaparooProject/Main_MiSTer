#pragma once

#include <stdbool.h>

// Kiosk mode: the OSD is unreachable, for card-only setups. It does not touch
// the menu background, so the user's own choice (the default snow, a
// wallpaper, a test pattern) is whatever shows. Set from the OSD behind a
// confirmation, or by a zaparoo_kiosk command on /dev/MiSTer_cmd.
bool zaparoo_kiosk_active(void);

// Persists, then applies immediately. Enabling closes the OSD: the input gates
// go live at once, so an open OSD would be stranded with ESC already dead.
void zaparoo_kiosk_set(bool on);

// Session-only OSD bypass. Lifts the gates without touching the setting, so a
// card can open the OSD for a look and another can put it away. Not persisted,
// and lost on the next core load because that re-execs Main, so a bypass can
// never be left behind.
bool zaparoo_kiosk_bypassed(void);
void zaparoo_kiosk_osd_open(void);
void zaparoo_kiosk_osd_close(void);
