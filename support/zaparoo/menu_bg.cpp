#include "menu_bg.h"

#include "alt_launcher.h"

#include "user_io.h"

#define MENU_STATUS_SNOW "[10]"

static int s_snow = -1;

static void set_snow(bool on)
{
	if (s_snow == (on ? 1 : 0)) return;
	s_snow = on ? 1 : 0;
	user_io_status_set(MENU_STATUS_SNOW, s_snow);
}

void zaparoo_menu_bg_init(void)
{
	// The launcher is queued right after this, so ownership is not visible
	// yet: a configured frontend starts black.
	s_snow = -1;
	set_snow(!alt_launcher_configured());
}

void zaparoo_menu_bg_poll(void)
{
	if (!is_menu()) return;
	set_snow(alt_launcher_menu_snow_allowed());
}
