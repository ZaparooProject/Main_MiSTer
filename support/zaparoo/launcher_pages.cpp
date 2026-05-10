#include "launcher_pages.h"
#include "alt_launcher.h"
#include "osd.h"

#include <stdio.h>

// Mirrors menu.cpp's STD_EXIT (a local #define there, kept in sync
// here rather than re-exporting it via a header touch).
#define LAUNCHER_STD_EXIT "            exit"

void launcher_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Zaparoo Launcher", OSD_ARROW_LEFT);
	*menumask = 0x3; // Video, Exit

	int m = 0;
	OsdWrite(m++);
	OsdWrite(m++, "");
	OsdWrite(m++, "");
	OsdWrite(m++, "");
	OsdWrite(m++, "");

	OsdWrite(m++, " Video                     \x16", menusub == 0);

	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, LAUNCHER_STD_EXIT, menusub == 1);
}

int launcher_page_handle_select(int menusub)
{
	switch (menusub)
	{
	case 0: return 1;
	case 1: return 0;
	default: return -1;
	}
}

void video_page_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	// No arrow flags: left/right are bound to value adjustment on this
	// page, not sibling navigation.
	OsdSetTitle("Video", 0);
	*menumask = 0xF; // CRT, H Offset, V Offset, Exit

	char s[64];
	int m = 0;

	OsdWrite(m++);
	OsdWrite(m++, "");
	OsdWrite(m++, "");

	sprintf(s, " CRT mode: %-15s", alt_launcher_native_crt() ? "On" : "Off");
	OsdWrite(m++, s, menusub == 0);

	OsdWrite(m++, "");
	sprintf(s, " H Offset: %+3d", alt_launcher_h_offset());
	OsdWrite(m++, s, menusub == 1);

	sprintf(s, " V Offset: %+3d", alt_launcher_v_offset());
	OsdWrite(m++, s, menusub == 2);

	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, LAUNCHER_STD_EXIT, menusub == 3);
}

bool video_page_handle_select(int menusub)
{
	switch (menusub)
	{
	case 0:
		alt_launcher_toggle_crt();
		return true;
	case 3:
		return false;
	default:
		return true;
	}
}

void video_page_adjust(int menusub, int dir)
{
	if (dir == 0) return;
	int8_t step = (int8_t)(dir > 0 ? 1 : -1);
	switch (menusub)
	{
	case 0:
		alt_launcher_toggle_crt();
		break;
	case 1:
		alt_launcher_set_h_offset((int8_t)(alt_launcher_h_offset() + step));
		break;
	case 2:
		alt_launcher_set_v_offset((int8_t)(alt_launcher_v_offset() + step));
		break;
	default:
		break;
	}
}
