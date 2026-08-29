#include "command.h"

#include <stdio.h>
#include <string.h>

#include "alt_launcher.h"
#include "kiosk.h"

// Tolerates trailing whitespace/CR: commands arrive from shell echoes.
static bool tok_eq(const char *p, const char *tok)
{
	size_t n = strlen(tok);
	if (strncmp(p, tok, n)) return false;
	p += n;
	while (*p == ' ' || *p == '\t' || *p == '\r') p++;
	return *p == 0;
}

// Returns the requested state, or -1 if the argument is not recognised.
static int parse_toggle(const char *arg, bool current)
{
	while (*arg == ' ' || *arg == '\t') arg++;
	if (tok_eq(arg, "on")) return 1;
	if (tok_eq(arg, "off")) return 0;
	if (tok_eq(arg, "toggle")) return current ? 0 : 1;
	return -1;
}

bool zaparoo_command(const char *cmd)
{
	if (!strncmp(cmd, "zaparoo_console ", 16)) return alt_launcher_command(cmd);

	if (!strncmp(cmd, "zaparoo_kiosk ", 14))
	{
		int state = parse_toggle(cmd + 14, zaparoo_kiosk_active());
		if (state < 0) printf("zaparoo_command: bad argument: %s\n", cmd);
		else zaparoo_kiosk_set(state != 0);
		return true;
	}

	if (!strncmp(cmd, "zaparoo_frontend ", 17))
	{
		int state = parse_toggle(cmd + 17, alt_launcher_enabled());
		if (state < 0) printf("zaparoo_command: bad argument: %s\n", cmd);
		else alt_launcher_set_enabled(state != 0);
		return true;
	}

	// Temporary peek at the OSD without unlocking the machine.
	if (!strncmp(cmd, "zaparoo_osd ", 12))
	{
		const char *arg = cmd + 12;
		while (*arg == ' ' || *arg == '\t') arg++;
		if (tok_eq(arg, "open")) zaparoo_kiosk_osd_open();
		else if (tok_eq(arg, "close")) zaparoo_kiosk_osd_close();
		else if (tok_eq(arg, "toggle"))
		{
			if (zaparoo_kiosk_bypassed()) zaparoo_kiosk_osd_close();
			else zaparoo_kiosk_osd_open();
		}
		else printf("zaparoo_command: bad argument: %s\n", cmd);
		return true;
	}

	printf("zaparoo_command: unknown command: %s\n", cmd);
	return false;
}
