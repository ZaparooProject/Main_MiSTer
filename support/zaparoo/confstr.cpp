#include "confstr.h"

#include <stdio.h>
#include <string.h>

#include "user_io.h"

// Steps over the prefixes menu.cpp strips before looking at a row's type.
static char *strip_prefixes(char *p)
{
	// Hide/disable flags come in pairs. We are locating a slot, not rendering
	// a menu, so their state is deliberately ignored: a slot the core is
	// currently hiding still occupies its position.
	while ((p[0] == 'H' || p[0] == 'D' || p[0] == 'h' || p[0] == 'd') && strlen(p) > 2) p += 2;
	if (p[0] == 'P' && p[1] >= '0' && p[1] <= '9' && p[2] != ',') p += 2;
	return p;
}

// Returns the row's own slot number, mirroring menu.cpp: a non-digit means
// slot 0, except on x86/PCXT where the character is used as-is.
static int row_slot(const char *p, int idx)
{
	if ((p[idx] >= '0' && p[idx] <= '9') || is_x86() || is_pcxt()) return p[idx] - '0';
	return 0;
}

static char *find_slot_row(int pos, int *core_slot)
{
	if (pos < 1) return 0;

	int seen = 0;
	int i = 2;
	for (;;)
	{
		char *p = user_io_get_confstr(i++);
		if (!p) return 0;

		p = strip_prefixes(p);
		if (p[0] != 'S') continue;

		// SC is the store-name form; the slot character sits one further on.
		int idx = (p[1] == 'C') ? 2 : 1;
		if (++seen != pos) continue;

		if (core_slot) *core_slot = row_slot(p, idx);
		return p;
	}
}

int zaparoo_confstr_slot_count(void)
{
	int seen = 0;
	int i = 2;
	for (;;)
	{
		char *p = user_io_get_confstr(i++);
		if (!p) return seen;
		p = strip_prefixes(p);
		if (p[0] == 'S') seen++;
	}
}

bool zaparoo_confstr_slot(int pos, int *core_slot,
                          char *ext, int ext_size,
                          char *label, int label_size)
{
	if (ext && ext_size) ext[0] = 0;
	if (label && label_size) label[0] = 0;

	char *p = find_slot_row(pos, core_slot);
	if (!p) return false;

	if (ext && ext_size)
	{
		char tmp[256] = {};
		substrcpy(tmp, p, 1);
		while (strlen(tmp) % 3) strcat(tmp, " ");
		if ((int)strlen(tmp) >= ext_size) return false;
		strcpy(ext, tmp);
	}

	if (label && label_size)
	{
		char tmp[256] = {};
		substrcpy(tmp, p, 2);
		snprintf(label, label_size, "%s", tmp);
	}

	return true;
}
