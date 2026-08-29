#include "kiosk.h"

#include <stdio.h>

#include "alt_launcher.h"
#include "settings.h"

#include "menu.h"
#include "user_io.h"
#include "video.h"

bool zaparoo_kiosk_active(void)
{
	return zaparoo_settings_kiosk_active();
}

void zaparoo_kiosk_set(bool on)
{
	if (zaparoo_kiosk_active() == on) return;
	if (!zaparoo_settings_set_kiosk(on)) return;
	printf("zaparoo_kiosk: %s\n", on ? "on" : "off");

	// cfg.fb_terminal is fixed at cfg_parse time, so bring the menu-core
	// background in line now rather than waiting for the next core load.
	// video_menu_bg(0) clears menu_bg before the fb-disable override reads it,
	// so the core scans its own video. status[3:1] is left alone: it holds the
	// user's wallpaper choice, needed to restore on the way back out.
	if (is_menu() && !alt_launcher_configured())
		video_menu_bg(on ? 0 : user_io_status_get("[3:1]"));

	if (on && menu_present()) MenuHide();
}
