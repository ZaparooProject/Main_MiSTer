#include "cheat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"

// Case-insensitive substring test. strcasestr is a GNU extension and the
// cross-toolchain does not reliably expose it under the flags this builds with.
static bool contains_ci(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	if (!n) return false;
	for (const char *p = hay; *p; p++) if (!strncasecmp(p, needle, n)) return true;
	return false;
}

// Accepts a 0-based index or a name. An exact (case-insensitive) name wins;
// otherwise a substring match is allowed, but only when it is unambiguous, so
// "Infinite HP" finds "Infinite HP.gg" without ever guessing between two
// similarly named entries.
static int resolve(const char *arg)
{
	int count = cheats_available();

	char *end = 0;
	long idx = strtol(arg, &end, 10);
	while (end && (*end == ' ' || *end == '\t' || *end == '\r')) end++;
	if (end != arg && end && !*end)
	{
		if (idx < 0 || idx >= count)
		{
			printf("zaparoo_cheat: index %ld out of range (0-%d)\n", idx, count - 1);
			return -1;
		}
		return (int)idx;
	}

	for (int i = 0; i < count; i++)
	{
		const char *name = cheats_get_name(i);
		if (name && !strcasecmp(name, arg)) return i;
	}

	int found = -1;
	for (int i = 0; i < count; i++)
	{
		const char *name = cheats_get_name(i);
		if (!name || !contains_ci(name, arg)) continue;
		if (found >= 0)
		{
			printf("zaparoo_cheat: \"%s\" matches more than one cheat\n", arg);
			return -1;
		}
		found = i;
	}

	if (found < 0) printf("zaparoo_cheat: no cheat matching \"%s\"\n", arg);
	return found;
}

// cheats_toggle() acts on the cheats menu's own cursor, so borrow it and put
// the cursor back: an external command must not drag the selection under a user
// who has that menu open.
static bool set_enabled(int idx, bool want)
{
	if (cheats_get_enabled(idx) == want) return true;

	int restore = cheats_get_selected();
	cheats_set_selected(idx);
	cheats_toggle();
	cheats_set_selected(restore);

	// Enabling lazily loads the cheat file and can fail on a bad or oversized
	// one, in which case the core state is unchanged.
	if (cheats_get_enabled(idx) != want)
	{
		printf("zaparoo_cheat: could not turn %s \"%s\"\n",
		            want ? "on" : "off", cheats_get_name(idx));
		return false;
	}
	return true;
}

// A full set can run to thousands of entries, so an unfiltered dump lists only
// what is switched on. Pass a filter to search the rest.
#define ZCHEAT_LIST_MAX 60

static void list(const char *filter)
{
	int count = cheats_available();
	int shown = 0, matched = 0;

	for (int i = 0; i < count; i++)
	{
		const char *name = cheats_get_name(i);
		if (!name) continue;
		if (filter ? !contains_ci(name, filter) : !cheats_get_enabled(i)) continue;
		matched++;
		if (shown >= ZCHEAT_LIST_MAX) continue;
		shown++;
		printf("  [%d] %s %s\n", i, cheats_get_enabled(i) ? "on " : "off", name);
	}

	if (filter)
		printf("zaparoo_cheat: %d of %d match \"%s\"%s\n", matched, count, filter,
		            matched > shown ? ", listing truncated" : "");
	else
		printf("zaparoo_cheat: %d of %d on. Use 'zaparoo_cheat list <text>' to search\n",
		            matched, count);
}

static void clear(void)
{
	int count = cheats_available();
	int restore = cheats_get_selected();
	int n = 0;
	for (int i = 0; i < count; i++)
	{
		if (!cheats_get_enabled(i)) continue;
		cheats_set_selected(i);
		cheats_toggle();
		n++;
	}
	cheats_set_selected(restore);
	printf("zaparoo_cheat: cleared %d cheat(s)\n", n);
}

void zaparoo_cheat_command(const char *cmd)
{
	while (*cmd == ' ' || *cmd == '\t') cmd++;

	// Trim trailing whitespace/CR: commands arrive from shell echoes.
	char arg[320];
	snprintf(arg, sizeof(arg), "%s", cmd);
	for (int i = (int)strlen(arg) - 1; i >= 0 && (arg[i] == '\r' || arg[i] == '\n' || arg[i] == ' ' || arg[i] == '\t'); i--)
		arg[i] = 0;

	// Cheats are loaded per ROM, and cheats_init() wipes core-side state on
	// every load, so an early command has nothing to act on.
	if (!cheats_available())
	{
		printf("zaparoo_cheat: no cheats loaded for this game\n");
		return;
	}

	if (!strcasecmp(arg, "list")) { list(0); return; }
	if (!strcasecmp(arg, "clear")) { clear(); return; }

	const char *rest = strchr(arg, ' ');
	if (!rest)
	{
		printf("zaparoo_cheat: expected on|off|toggle <name|index>, clear, or list [text]\n");
		return;
	}

	size_t verb_len = rest - arg;
	while (*rest == ' ' || *rest == '\t') rest++;
	if (!*rest)
	{
		printf("zaparoo_cheat: expected on|off|toggle <name|index>, clear, or list [text]\n");
		return;
	}

	if (verb_len == 4 && !strncasecmp(arg, "list", 4)) { list(rest); return; }

	int idx = resolve(rest);
	if (idx < 0) return;

	bool want;
	if (verb_len == 2 && !strncasecmp(arg, "on", 2)) want = true;
	else if (verb_len == 3 && !strncasecmp(arg, "off", 3)) want = false;
	else if (verb_len == 6 && !strncasecmp(arg, "toggle", 6)) want = !cheats_get_enabled(idx);
	else
	{
		printf("zaparoo_cheat: expected on|off|toggle <name|index>, clear, or list [text]\n");
		return;
	}

	if (set_enabled(idx, want))
		printf("zaparoo_cheat: \"%s\" %s\n", cheats_get_name(idx), want ? "on" : "off");
}
