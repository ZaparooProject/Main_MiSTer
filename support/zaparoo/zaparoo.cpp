#include "zaparoo.h"
#include <stddef.h>
#include "../../cfg.h"
#include "../../user_io.h"
#include <linux/input.h>

void zaparoo_cfg_defaults(void)
{
	cfg.recents = 1;
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
