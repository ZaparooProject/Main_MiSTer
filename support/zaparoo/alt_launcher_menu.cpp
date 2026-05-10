#include "alt_launcher_menu.h"
#include "alt_launcher.h"
#include "file_io.h"
#include "osd.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

extern const char *version;

// The OSD column used for the Exit row label, sized to match
// menu.cpp's STD_EXIT define (a local #define there, kept in sync
// here rather than re-exported to avoid a header touch).
#define ALT_STD_EXIT "            exit"

int alt_launcher_render_system_menu(int menusub, uint64_t *menumask,
                                    int *reboot_req,
                                    long *sysinfo_timer)
{
	if (!alt_launcher_configured()) return 0;

	char s[256];
	int m = 0;

	OsdSetTitle("System Settings", OSD_ARROW_LEFT);
	*menumask = 0x1F;

	OsdWrite(m++);
	sprintf(s, "       MiSTer v%s", version + 5);
	{
		char str[8] = {};
		FILE *f = fopen("/MiSTer.version", "r");
		if (f)
		{
			if (fread(str, 6, 1, f)) sprintf(s, " MiSTer v%s,  OS v%s", version + 5, str);
			fclose(f);
		}
	}
	OsdWrite(m++, s);

	{
		uint64_t avail = 0;
		struct statvfs buf;
		memset(&buf, 0, sizeof(buf));
		if (!statvfs(getRootDir(), &buf)) avail = buf.f_bsize * buf.f_bavail;
		if (avail < (10ull * 1024 * 1024 * 1024))
			sprintf(s, "   Available space: %llumb", (unsigned long long)(avail / (1024 * 1024)));
		else
			sprintf(s, "   Available space: %llugb", (unsigned long long)(avail / (1024 * 1024 * 1024)));
		OsdWrite(m + 2, s, 0, 0);
	}

	OsdWrite(m++, "");
	OsdWrite(m++, "");
	m++;
	OsdWrite(m++, "");
	OsdWrite(m++, "");

	OsdWrite(m++, " Remap keyboard            \x16", menusub == 0);
	OsdWrite(m++, " Define joystick buttons   \x16", menusub == 1);

	sprintf(s, " CRT mode: %-15s", alt_launcher_native_crt() ? "On" : "Off");
	OsdWrite(m++, s, menusub == 2);

	OsdWrite(m++, "");
	int cr = m;
	OsdWrite(m++, " Reboot (hold \x16 cold reboot)", menusub == 3);
	*sysinfo_timer = 0;
	*reboot_req = 0;

	while (m < OsdGetSize() - 1) OsdWrite(m++, "");
	OsdWrite(15, ALT_STD_EXIT, menusub == 4);

	return cr;
}

int alt_launcher_translate_system_select(int menusub)
{
	if (!alt_launcher_configured()) return menusub;

	if (menusub == 2)
	{
		alt_launcher_toggle_crt();
		return -1;
	}

	static const int map[] = { 1, 2, -1, 5, 6 };
	if (menusub < 0 || menusub >= (int)(sizeof(map) / sizeof(map[0]))) return -1;
	return map[menusub];
}

bool alt_launcher_system_holding_reboot(int menusub)
{
	return alt_launcher_configured() && menusub == 3;
}
