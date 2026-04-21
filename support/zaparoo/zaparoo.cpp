#ifdef ZAPAROO
#include "zaparoo.h"
#include "../../menu.h"
#include "../../user_io.h"
#include <string.h>

void zaparoo_publish_features(void)
{
	MakeFile(ZAPAROO_FEATURES_FILE, "PICKER,NOTICE");
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
#endif
