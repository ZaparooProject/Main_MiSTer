#include "menu_rbf.h"
#include <string.h>
#include <strings.h>

static const char s_menu_rbf_path[] = "zaparoo/menu_zaparoo.rbf";

const char *menu_rbf_name(void)
{
	return s_menu_rbf_path;
}

bool is_menu_rbf(const char *name)
{
	if (!name || !name[0]) return false;
	if (!strcasecmp(name, "menu.rbf")) return true;
	if (!strcasecmp(name, s_menu_rbf_path)) return true;
	const char *base = strrchr(s_menu_rbf_path, '/');
	base = base ? base + 1 : s_menu_rbf_path;
	if (base[0] && !strcasecmp(name, base)) return true;
	return false;
}

bool is_zaparoo_menu_bootcore(const char *name)
{
	if (!name || !name[0]) return false;
	if (!strcasecmp(name, s_menu_rbf_path)) return true;
	const char *base = strrchr(s_menu_rbf_path, '/');
	base = base ? base + 1 : s_menu_rbf_path;
	return base[0] && !strcasecmp(name, base);
}
