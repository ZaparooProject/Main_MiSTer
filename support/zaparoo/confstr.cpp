#include "confstr.h"

#include <stdio.h>
#include <string.h>

#include "spi.h"
#include "user_io.h"

// substrcpy() has no destination bound and a config item can run to 2 KB, so
// every field copied out of one here goes through this instead. Same field
// semantics: returns the length copied, 0 for an empty or missing field.
static int substrcpy_n(char *d, size_t size, const char *s, char idx)
{
	char p = 0;
	size_t n = 0;
	if (!size) return 0;
	while (*s)
	{
		if (p == idx)
		{
			if (*s == ',') break;
			if (n < size - 1) d[n++] = *s;
		}
		else if (*s == ',') p++;
		s++;
	}
	d[n] = 0;
	return (int)n;
}

// Steps over the prefixes menu.cpp strips before looking at a row's type, and
// optionally resolves them the way menu.cpp does. Pass hdmask 0 when only the
// pointer advance is wanted: the flag results are then meaningless but the
// stride is identical, since the prefixes are fixed-width pairs.
static char *eval_prefixes(char *p, uint32_t hdmask, bool *hidden, bool *disabled)
{
	int h = 0, d = 0;
	while ((p[0] == 'H' || p[0] == 'D' || p[0] == 'h' || p[0] == 'd') && strlen(p) > 2)
	{
		int flg = (hdmask & (1 << user_io_hd_mask(p + 1))) ? 1 : 0;
		if (p[0] == 'H') h |= flg;
		if (p[0] == 'h') h |= (flg ^ 1);
		if (p[0] == 'D') d |= flg;
		if (p[0] == 'd') d |= (flg ^ 1);
		p += 2;
	}
	if (p[0] == 'P' && p[1] >= '0' && p[1] <= '9' && p[2] != ',') p += 2;
	if (hidden) *hidden = h;
	if (disabled) *disabled = d;
	return p;
}

// Slot lookup ignores hide/disable state: a slot the core is currently hiding
// still occupies its position, so positions stay stable.
static char *strip_prefixes(char *p)
{
	return eval_prefixes(p, 0, 0, 0);
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

bool zaparoo_confstr_autosave(char *opt, int opt_size, int *ex, uint32_t *on_value)
{
	int i = 2;
	for (;;)
	{
		char *p = user_io_get_confstr(i++);
		if (!p) return false;

		p = strip_prefixes(p);
		if (p[0] != 'O' && p[0] != 'o') continue;

		char label[64] = {};
		substrcpy_n(label, sizeof(label), p, 1);
		// Console cores call it plain "Autosave"; arcade cores qualify it
		// ("Autosave NVRAM" on CAVE, "Autosave Hiscores" on the shared nvram.v
		// cores), and those ship Off, which is what stops a forced save cold.
		if (strncasecmp(label, "Autosave", 8)) continue;
		if (label[8] && label[8] != ' ') continue;

		// Find which value means on rather than assuming index 1: a core is
		// free to list On first.
		for (int v = 0; v < 16; v++)
		{
			char val[64] = {};
			if (!substrcpy_n(val, sizeof(val), p, 2 + v) || !val[0]) break;
			if (strcasecmp(val, "On")) continue;

			snprintf(opt, opt_size, "%s", p + 1);
			if (ex) *ex = (p[0] == 'o');
			if (on_value) *on_value = (uint32_t)v;
			return true;
		}
		return false;
	}
}

// Console cores label this row "Save Backup RAM", except PSX ("Save Memory
// Cards") and NeoGeo ("Save Memory Card"); CAVE arcade cores use "Save NVRAM".
// Requiring the Save prefix and one of the known nouns covers the variants
// without ever matching a savestate row, which is always "Save state ...".
static bool is_save_row_label(const char *label)
{
	if (strncasecmp(label, "Save ", 5)) return false;
	const char *rest = label + 5;
	return !strcasecmp(rest, "Backup RAM") ||
	       !strcasecmp(rest, "Memory Card") ||
	       !strcasecmp(rest, "Memory Cards") ||
	       !strcasecmp(rest, "NVRAM");
}

bool zaparoo_confstr_save_row(char *opt, int opt_size, int *ex, bool *usable)
{
	uint32_t hdmask = spi_uio_cmd16(UIO_GET_OSDMASK, 0);

	int i = 2;
	for (;;)
	{
		char *p = user_io_get_confstr(i++);
		if (!p) return false;

		bool hidden = false, disabled = false;
		p = eval_prefixes(p, hdmask, &hidden, &disabled);
		// T and R are the same toggle to menu.cpp; R additionally closes the
		// menu, which is irrelevant here. CAVE uses T for its NVRAM row.
		if (p[0] != 'R' && p[0] != 'r' && p[0] != 'T' && p[0] != 't') continue;

		char label[64] = {};
		substrcpy_n(label, sizeof(label), p, 1);
		if (!is_save_row_label(label)) continue;

		snprintf(opt, opt_size, "%s", p + 1);
		if (ex) *ex = (p[0] == 'r') || (p[0] == 't');
		if (usable) *usable = !hidden && !disabled;
		return true;
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
		size_t len = substrcpy_n(tmp, sizeof(tmp), p, 1);
		while (len % 3 && len < sizeof(tmp) - 1) tmp[len++] = ' ';
		tmp[len] = 0;
		if ((int)len >= ext_size) return false;
		strcpy(ext, tmp);
	}

	if (label && label_size)
	{
		char tmp[256] = {};
		substrcpy_n(tmp, sizeof(tmp), p, 2);
		snprintf(label, label_size, "%s", tmp);
	}

	return true;
}
