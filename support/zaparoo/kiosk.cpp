#include "kiosk.h"

#include <stdio.h>

#include <linux/input.h>

#include "settings.h"

#include "hardware.h"
#include "input.h"
#include "menu.h"

static bool s_bypass = false;
static unsigned long s_key_at = 0;

bool zaparoo_kiosk_active(void)
{
	return zaparoo_settings_kiosk_active() && !s_bypass;
}

bool zaparoo_kiosk_bypassed(void)
{
	return s_bypass;
}

void zaparoo_kiosk_set(bool on)
{
	if (zaparoo_settings_kiosk_active() == on) return;
	if (!zaparoo_settings_set_kiosk(on)) return;
	printf("zaparoo_kiosk: %s\n", on ? "on" : "off");

	s_bypass = false;

	// The menu background is not kiosk's business: whatever the user picked
	// (snow, wallpaper, a test pattern) stays as it is.
	if (on && menu_present()) MenuHide();
}

void zaparoo_kiosk_osd_open(void)
{
	if (!zaparoo_settings_kiosk_active() || s_bypass) return;
	s_bypass = true;
	printf("zaparoo_kiosk: OSD bypass on\n");
	// Same synthetic event user_io raises for a real F12/MENU release, so the
	// OSD opens on a game core too, not just where the menu auto-opens.
	//
	// menu_key is a single slot and menu_key_get() only acts on a *change*
	// (`if (c1 != c2)`), so re-sending the same release is silently dropped and
	// this would work exactly once per session. Real keys escape that because
	// press and release alternate. Clear the slot, then send the release once
	// the 20 ms debounce in menu_key_get() has latched the cleared value, which
	// guarantees a genuine edge every time.
	menu_key_set(0);
	s_key_at = GetTimer(60);
}

// Driven from zaparoo_poll(): delivers the release armed by osd_open above.
void zaparoo_kiosk_poll(void)
{
	if (!s_key_at || !CheckTimer(s_key_at)) return;
	s_key_at = 0;
	menu_key_set(KEY_F12 | UPSTROKE);
}

void zaparoo_kiosk_osd_close(void)
{
	if (!s_bypass) return;
	// Clear first: MenuHide() runs HandleUI, which would re-open the OSD on
	// the menu core while the gates are still lifted.
	s_bypass = false;
	s_key_at = 0;
	printf("zaparoo_kiosk: OSD bypass off\n");
	if (menu_present()) MenuHide();
}
