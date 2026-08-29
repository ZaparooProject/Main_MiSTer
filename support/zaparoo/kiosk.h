#pragma once

#include <stdbool.h>

// Kiosk mode: the OSD is unreachable and the menu core shows its own video
// (snow). Set from the OSD behind a confirmation, or by a zaparoo_kiosk
// command on /dev/MiSTer_cmd, which is the only way back out.
bool zaparoo_kiosk_active(void);

// Persists, then applies immediately. Enabling closes the OSD: the input gates
// go live at once, so an open OSD would be stranded with ESC already dead.
void zaparoo_kiosk_set(bool on);
