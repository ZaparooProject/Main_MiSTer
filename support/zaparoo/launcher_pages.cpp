#include "launcher_pages.h"
#include "alt_launcher.h"
#include "crt_settings.h"
#include "settings.h"
#include "osd.h"

#include <stdio.h>

// Mirrors menu.cpp's STD_EXIT / STD_BACK (local #defines there).
#define PAGE_STD_EXIT "            exit"
#define PAGE_STD_BACK "            back"

void frontend_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Zaparoo", OSD_ARROW_LEFT);
	bool crt = alt_launcher_native_crt_persisted();
	*menumask = crt ? 0xFF : 0x9F;

	char s[64];
	int m = 0;
	OsdWrite(m++, "");
	// enabled(), not configured(): the row must not read Off just because the
	// user quit the frontend this session.
	sprintf(s, " Frontend:               %s", alt_launcher_enabled() ? " On" : "Off");
	OsdWrite(m++, s, menusub == 0);
	// The setting, not zaparoo_kiosk_active(): that is gated by the session
	// bypass, which is exactly how this page gets reached while kiosk is on.
	sprintf(s, " Kiosk mode:             %s", zaparoo_settings_kiosk_active() ? " On" : "Off");
	OsdWrite(m++, s, menusub == 1);
	sprintf(s, " Auto-save:              %s", zaparoo_settings_save_on_exit() ? " On" : "Off");
	OsdWrite(m++, s, menusub == 2);
	sprintf(s, " Auto-run discs:         %s", zaparoo_settings_cd_autorun() ? " On" : "Off");
	OsdWrite(m++, s, menusub == 3);
	OsdWrite(m++, "");
	sprintf(s, " CRT mode:               %s", crt ? " On" : "Off");
	OsdWrite(m++, s, menusub == 4);
	if (crt)
	{
		sprintf(s, " Video standard:         %4s", crt_standard_name(alt_launcher_native_crt_mode()));
		OsdWrite(m++, s, menusub == 5);
		OsdWrite(m++, " Screen position           \x16", menusub == 6);
	}
	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, PAGE_STD_EXIT, menusub == 7);
}

bool frontend_page_row_has_submenu(int menusub)
{
	return menusub == 6;
}

int frontend_page_select(int menusub)
{
	switch (menusub)
	{
	case 0:
		alt_launcher_set_enabled(!alt_launcher_enabled());
		return 0;
	case 1:
		// Reachable with kiosk already on via the bypass, so turning it off
		// has to work here. Only turning it on needs the warning.
		if (zaparoo_settings_kiosk_active())
		{
			zaparoo_kiosk_set(false);
			return 0;
		}
		return 3;
	case 2:
		// Turning it off needs no confirmation; turning it on explains itself
		// first, because the name promises more than it can deliver.
		if (zaparoo_settings_save_on_exit())
		{
			zaparoo_settings_set_save_on_exit(false);
			return 0;
		}
		return 4;
	case 3:
		zaparoo_settings_set_cd_autorun(!zaparoo_settings_cd_autorun());
		return 0;
	case 4:
		alt_launcher_toggle_native_crt();
		return 0;
	case 5:
		alt_launcher_set_native_crt_mode(crt_standard_next(alt_launcher_native_crt_mode()));
		return 0;
	case 6:
		return 1;
	default:
		return 2;
	}
}

void kiosk_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Warning!!!", 0);
	*menumask = 3;

	int m = 0;
	OsdWrite(m++, "");
	OsdWrite(m++, "         Attention:");
	OsdWrite(m++, " Kiosk mode locks the OSD.");
	OsdWrite(m++, "");
	OsdWrite(m++, " The menu cannot be opened");
	OsdWrite(m++, " from a keyboard, gamepad");
	OsdWrite(m++, " or the console buttons.");
	OsdWrite(m++, "");
	OsdWrite(m++, " To undo it you need a");
	OsdWrite(m++, " Zaparoo card set up first,");
	OsdWrite(m++, " or delete the settings file");
	OsdWrite(m++, " from the SD card on a PC.");
	OsdWrite(m++, "");
	OsdWrite(m++, "  Do you want to continue?");
	OsdWrite(m++, "            No", menusub == 0);
	OsdWrite(m++, "            Yes", menusub == 1);
}

void autosave_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Warning!!!", 0);
	*menumask = 3;

	int m = 0;
	OsdWrite(m++, "");
	OsdWrite(m++, "         Attention:");
	OsdWrite(m++, " Auto-save runs when one");
	OsdWrite(m++, " game exits and another");
	OsdWrite(m++, " starts.");
	OsdWrite(m++, "");
	OsdWrite(m++, " Switching the MiSTer off");
	OsdWrite(m++, " mid-game still loses the");
	OsdWrite(m++, " save.");
	OsdWrite(m++, "");
	OsdWrite(m++, " Adds about half a second");
	OsdWrite(m++, " to each game launch.");
	OsdWrite(m++, "");
	OsdWrite(m++, "  Do you want to continue?");
	OsdWrite(m++, "            No", menusub == 0);
	OsdWrite(m++, "            Yes", menusub == 1);
}

bool autosave_page_confirm(int menusub)
{
	if (menusub != 1) return false;
	return zaparoo_settings_set_save_on_exit(true);
}

bool kiosk_page_confirm(int menusub)
{
	if (menusub != 1) return false;
	// Closes the OSD itself: the input gates go live immediately, so an open
	// OSD would be stranded with ESC already dead.
	return zaparoo_kiosk_set(true);
}

static int s_h, s_v, s_h0, s_v0;
static int s_size, s_size0;
static int s_vsize, s_vsize0;
static bool s_pattern;

static bool position_live(void)
{
	return s_pattern || alt_launcher_native_crt();
}

static int clamp_int(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

void position_page_enter(void)
{
	crt_toml_get_offsets(&s_h, &s_v);
	crt_toml_get_hsize(&s_size);
	crt_toml_get_vsize(&s_vsize);
	s_h0 = s_h;
	s_v0 = s_v;
	s_size0 = s_size;
	s_vsize0 = s_vsize;
	s_pattern = false;
	if (alt_launcher_native_crt_persisted() && !alt_launcher_active())
		s_pattern = crt_test_pattern_publish(alt_launcher_native_crt_mode(), s_h, s_v);
}

void position_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Position", 0);
	*menumask = 0x1F;

	char s[64];
	int m = 0;
	OsdWrite(m++, "");
	sprintf(s, " H offset:               %+3d", s_h);
	OsdWrite(m++, s, menusub == 0);
	sprintf(s, " V offset:               %+3d", s_v);
	OsdWrite(m++, s, menusub == 1);
	sprintf(s, " H size:                 %+3d", s_size);
	OsdWrite(m++, s, menusub == 2);
	sprintf(s, " V size:                 %+3d", s_vsize);
	OsdWrite(m++, s, menusub == 3);
	OsdWrite(m++, "");
	OsdWrite(m++, position_live() ? " Left/Right: adjust picture" : " No CRT picture to adjust");
	OsdWrite(m++, " (+2 size narrows position)");
	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, PAGE_STD_BACK, menusub == 4);
}

void position_page_adjust(int menusub, int dir)
{
	if (menusub == 0) s_h = clamp_int(s_h + dir, CRT_H_OFFSET_MIN, CRT_H_OFFSET_MAX);
	else if (menusub == 1) s_v = clamp_int(s_v + dir, CRT_V_OFFSET_MIN, CRT_V_OFFSET_MAX);
	else if (menusub == 2)
	{
		s_size = clamp_int(s_size + dir, CRT_H_SIZE_MIN, CRT_H_SIZE_MAX);
		if (position_live()) crt_size_apply_live(s_size, s_vsize);
		return;
	}
	else if (menusub == 3)
	{
		// Shrink only: blank lines lower the frame rate, they can't add
		// active lines.
		s_vsize = clamp_int(s_vsize + dir, CRT_V_SIZE_MIN, CRT_V_SIZE_MAX);
		if (position_live()) crt_size_apply_live(s_size, s_vsize);
		return;
	}
	else return;
	if (position_live()) crt_offsets_apply_live(s_h, s_v, alt_launcher_native_crt_mode());
}

bool position_page_is_exit(int menusub)
{
	return menusub == 4;
}

void position_page_leave(void)
{
	if (s_pattern)
	{
		crt_test_pattern_unpublish();
		s_pattern = crt_test_pattern_active();
	}
	if (s_size != s_size0)
	{
		// Size needs no respawn: the frontend never reads, caches, or writes
		// crt_h_size (or DDR bytes >= 8), so the live word2 write plus this
		// TOML save is already complete.
		if (crt_toml_set_hsize(s_size)) s_size0 = s_size;
		else printf("launcher_pages: h size not saved\n");
	}
	if (s_vsize != s_vsize0)
	{
		// Same contract as crt_h_size: Main-owned, no respawn needed.
		if (crt_toml_set_vsize(s_vsize)) s_vsize0 = s_vsize;
		else printf("launcher_pages: v size not saved\n");
	}
	if (s_h == s_h0 && s_v == s_v0) return;
	if (!crt_toml_set_offsets(s_h, s_v))
	{
		printf("launcher_pages: offsets not saved, frontend left running\n");
		return;
	}
	s_h0 = s_h;
	s_v0 = s_v;
	// A running frontend caches its trims at start and would write the old
	// values back on its next settings save.
	if (alt_launcher_active()) alt_launcher_respawn();
}
