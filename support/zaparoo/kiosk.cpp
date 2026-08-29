#include "kiosk.h"

#include <stdio.h>

#include "settings.h"

#include "menu.h"

bool zaparoo_kiosk_active(void)
{
	return zaparoo_settings_kiosk_active();
}

void zaparoo_kiosk_set(bool on)
{
	if (zaparoo_kiosk_active() == on) return;
	if (!zaparoo_settings_set_kiosk(on)) return;
	printf("zaparoo_kiosk: %s\n", on ? "on" : "off");

	// The menu background is not kiosk's business: whatever the user picked
	// (snow, wallpaper, a test pattern) stays as it is.
	if (on && menu_present()) MenuHide();
}
