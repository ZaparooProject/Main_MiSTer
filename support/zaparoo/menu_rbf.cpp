#include "menu_rbf.h"
#include <string.h>
#include <strings.h>
#include "../../cfg.h"

const char *menu_rbf_name(void)
{
	return cfg.menu_rbf[0] ? cfg.menu_rbf : "menu.rbf";
}

bool is_menu_rbf(const char *name)
{
	if (!name || !name[0]) return false;
	if (!strcasecmp(name, "menu.rbf")) return true;
	if (cfg.menu_rbf[0])
	{
		if (!strcasecmp(name, cfg.menu_rbf)) return true;
		const char *slash = strrchr(cfg.menu_rbf, '/');
		const char *base = slash ? slash + 1 : cfg.menu_rbf;
		if (base[0] && !strcasecmp(name, base)) return true;
	}
	return false;
}
