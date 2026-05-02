#include "zaparoo.h"
#include <stddef.h>
#include "../../cfg.h"
#include "../../menu.h"
#include "../../user_io.h"
#include <linux/input.h>
#include <string.h>

void zaparoo_cfg_defaults(void)
{
	cfg.recents = 1;
}

void zaparoo_publish_features(void)
{
	MakeFile(ZAPAROO_FEATURES_FILE, "PICKER,NOTICE");
}

uint16_t zaparoo_fb_terminal_key(uint32_t mask, bool osd_button)
{
	if (!cfg.alt_launcher[0])
		return 0;

	if (osd_button)
		return KEY_MENU;

	switch (mask)
	{
	case JOY_L2:
		return KEY_F1;
	case JOY_R2:
		return KEY_BACKSPACE;
	}

	return 0;
}

static void zaparoo_show_notice(const char *msg)
{
	InfoMessage(msg, 5000, "Notice");
}

bool zaparoo_handle_input_cmd(const char *cmd)
{
	if (!strcmp(cmd, "show_picker"))
	{
		menu_show_picker();
		return true;
	}
	if (!strncmp(cmd, "show_notice ", 12))
	{
		zaparoo_show_notice(cmd + 12);
		return true;
	}
	return false;
}
