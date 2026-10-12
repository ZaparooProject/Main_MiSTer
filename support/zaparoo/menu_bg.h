#pragma once

// Tells the Zaparoo menu core when its idle video may show snow. The core
// holds black unless the OSD is up or menu status bit 10 is set, so a frontend
// start has no snow flash. Main sets the bit whenever no frontend owns or is
// about to take the screen, which keeps the background visible with the OSD
// closed (kiosk mode, the idle screensaver, after quitting the frontend).
// A menu core without the bit ignores it.

// Called once the menu core's saved status has been loaded: the whole status
// word is saved by F1, so a stale bit must not survive into a frontend start.
void zaparoo_menu_bg_init(void);

// Driven from zaparoo_poll(): follows frontend ownership.
void zaparoo_menu_bg_poll(void);
