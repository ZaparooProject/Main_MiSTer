#include "launcher_pages.h"
#include "alt_launcher.h"
#include "crt_settings.h"
#include "osd.h"

#include <stdio.h>

// Mirrors menu.cpp's STD_EXIT / STD_BACK (local #defines there).
#define PAGE_STD_EXIT "            exit"
#define PAGE_STD_BACK "            back"

void frontend_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Frontend", OSD_ARROW_LEFT);
	bool crt = alt_launcher_native_crt_persisted();
	*menumask = crt ? 0xF : 0x9;

	char s[64];
	int m = 0;
	OsdWrite(m++, "");
	sprintf(s, " CRT mode:               %s", crt ? " On" : "Off");
	OsdWrite(m++, s, menusub == 0);
	if (crt)
	{
		sprintf(s, " Video standard:         %4s", crt_standard_name(alt_launcher_native_crt_mode()));
		OsdWrite(m++, s, menusub == 1);
		OsdWrite(m++, " Screen position           \x16", menusub == 2);
	}
	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, PAGE_STD_EXIT, menusub == 3);
}

int frontend_page_select(int menusub)
{
	switch (menusub)
	{
	case 0:
		alt_launcher_toggle_native_crt();
		return 0;
	case 1:
		alt_launcher_set_native_crt_mode(crt_standard_next(alt_launcher_native_crt_mode()));
		return 0;
	case 2:
		return 1;
	default:
		return 2;
	}
}

static int s_h, s_v, s_h0, s_v0;
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
	s_h0 = s_h;
	s_v0 = s_v;
	s_pattern = false;
	if (alt_launcher_native_crt_persisted() && !alt_launcher_active())
		s_pattern = crt_test_pattern_publish(alt_launcher_native_crt_mode(), s_h, s_v);
}

void position_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Position", 0);
	*menumask = 0x7;

	char s[64];
	int m = 0;
	OsdWrite(m++, "");
	sprintf(s, " H offset:               %+3d", s_h);
	OsdWrite(m++, s, menusub == 0);
	sprintf(s, " V offset:               %+3d", s_v);
	OsdWrite(m++, s, menusub == 1);
	OsdWrite(m++, "");
	OsdWrite(m++, position_live() ? " Left/Right: move picture" : " No CRT picture to adjust");
	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, PAGE_STD_BACK, menusub == 2);
}

void position_page_adjust(int menusub, int dir)
{
	if (menusub == 0) s_h = clamp_int(s_h + dir, CRT_H_OFFSET_MIN, CRT_H_OFFSET_MAX);
	else if (menusub == 1) s_v = clamp_int(s_v + dir, CRT_V_OFFSET_MIN, CRT_V_OFFSET_MAX);
	else return;
	if (position_live()) crt_offsets_apply_live(s_h, s_v, alt_launcher_native_crt_mode());
}

bool position_page_is_exit(int menusub)
{
	return menusub == 2;
}

void position_page_leave(void)
{
	if (s_pattern)
	{
		crt_test_pattern_unpublish();
		s_pattern = false;
	}
	if (s_h == s_h0 && s_v == s_v0) return;
	crt_toml_set_offsets(s_h, s_v);
	// A running frontend caches its trims at start and would write the old
	// values back on its next settings save.
	if (alt_launcher_active()) alt_launcher_respawn();
}
