#pragma once

#include <stdbool.h>

// Kiosk mode: the OSD is unreachable, for card-only setups. It does not touch
// the menu background, so the user's own choice (the default snow, a
// wallpaper, a test pattern) is whatever shows. Set from the OSD behind a
// confirmation, or by a zaparoo_kiosk command on /dev/MiSTer_cmd, which is the
// only way back out.
bool zaparoo_kiosk_active(void);

// Persists, then applies immediately. Enabling closes the OSD: the input gates
// go live at once, so an open OSD would be stranded with ESC already dead.
void zaparoo_kiosk_set(bool on);
