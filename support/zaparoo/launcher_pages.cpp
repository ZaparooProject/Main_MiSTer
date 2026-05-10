#include "display_menu.h"
#include "alt_launcher.h"
#include "osd.h"

#include <stdio.h>

#define DISPLAY_STD_EXIT "            exit"

void display_menu_render(int menusub, uint64_t *menumask)
{
	OsdSetSize(16);
	OsdSetTitle("Display Centering", OSD_ARROW_LEFT);
	*menumask = 0xF; // 4 entries: H, V, CRT, Exit

	char s[64];
	int m = 0;

	OsdWrite(m++);
	OsdWrite(m++, "");
	OsdWrite(m++, "");
	OsdWrite(m++, "");

	sprintf(s, " H Offset: %+3d  (-/+)", alt_launcher_h_offset());
	OsdWrite(m++, s, menusub == 0);

	sprintf(s, " V Offset: %+3d  (-/+)", alt_launcher_v_offset());
	OsdWrite(m++, s, menusub == 1);

	OsdWrite(m++, "");
	sprintf(s, " CRT mode: %-15s", alt_launcher_native_crt() ? "On" : "Off");
	OsdWrite(m++, s, menusub == 2);

	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, DISPLAY_STD_EXIT, menusub == 3);
}

bool display_menu_handle_select(int menusub)
{
	switch (menusub)
	{
	case 2:
		alt_launcher_toggle_crt();
		return true;
	case 3:
		return false;
	default:
		return true;
	}
}

void display_menu_adjust(int menusub, int dir)
{
	if (dir == 0) return;
	int8_t step = (int8_t)(dir > 0 ? 1 : -1);
	switch (menusub)
	{
	case 0:
		alt_launcher_set_h_offset((int8_t)(alt_launcher_h_offset() + step));
		break;
	case 1:
		alt_launcher_set_v_offset((int8_t)(alt_launcher_v_offset() + step));
		break;
	default:
		break;
	}
}
