#include "cheat_cmd.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cheats.h"

enum
{
	CHEAT_CMD_OK = 0,
	CHEAT_CMD_NO_CHEATS = -1,
	CHEAT_CMD_NOT_FOUND = -2,
	CHEAT_CMD_LOAD_FAILED = -3
};

static const char *skip_space(const char *s)
{
	while (*s && isspace((unsigned char)*s)) s++;
	return s;
}

static bool parse_token(const char **cmd, char *token, size_t token_size)
{
	const char *s = skip_space(*cmd);
	size_t len = 0;

	while (s[len] && !isspace((unsigned char)s[len])) len++;
	if (!len || len >= token_size) return false;

	memcpy(token, s, len);
	token[len] = 0;
	*cmd = s + len;
	return true;
}

static const char *cheat_cmd_status(int status)
{
	switch (status)
	{
	case CHEAT_CMD_OK: return "ok";
	case CHEAT_CMD_NO_CHEATS: return "no cheats loaded";
	case CHEAT_CMD_NOT_FOUND: return "not found";
	case CHEAT_CMD_LOAD_FAILED: return "load failed";
	default: return "error";
	}
}

static int find_cheat_by_name(const char *name)
{
	if (!name || !*name) return -1;

	for (int i = 0; i < cheats_available(); i++)
	{
		const char *cheat_name = cheats_get_name(i);
		if (cheat_name && !strcmp(cheat_name, name)) return i;
	}

	return -1;
}

static int toggle_index(int idx)
{
	if (!cheats_available()) return CHEAT_CMD_NO_CHEATS;
	if (idx < 0 || idx >= cheats_available()) return CHEAT_CMD_NOT_FOUND;

	bool was_enabled = cheats_get_enabled(idx);
	int old_entry = cheats_get_selected();
	cheats_set_selected(idx);
	cheats_toggle();
	cheats_set_selected(old_entry);

	return (cheats_get_enabled(idx) != was_enabled) ? CHEAT_CMD_OK : CHEAT_CMD_LOAD_FAILED;
}

static int set_index_enabled(int idx, bool enabled)
{
	if (!cheats_available()) return CHEAT_CMD_NO_CHEATS;
	if (idx < 0 || idx >= cheats_available()) return CHEAT_CMD_NOT_FOUND;
	if (cheats_get_enabled(idx) == enabled) return CHEAT_CMD_OK;

	return toggle_index(idx);
}

static int clear_enabled()
{
	if (!cheats_available()) return CHEAT_CMD_NO_CHEATS;

	int status = CHEAT_CMD_OK;
	for (int i = 0; i < cheats_available(); i++)
	{
		if (cheats_get_enabled(i))
		{
			int res = toggle_index(i);
			if (res != CHEAT_CMD_OK) status = res;
		}
	}

	return status;
}

void zaparoo_cheat_cmd(const char *cmd)
{
	char action[16];

	if (!parse_token(&cmd, action, sizeof(action)))
	{
		printf("MiSTer_cmd cheat: missing command\n");
		return;
	}

	if (!strcmp(action, "clear"))
	{
		int status = clear_enabled();
		printf("MiSTer_cmd cheat clear: %s\n", cheat_cmd_status(status));
		return;
	}

	const char *name = skip_space(cmd);
	if (!*name)
	{
		printf("MiSTer_cmd cheat %s: missing name\n", action);
		return;
	}

	int idx = find_cheat_by_name(name);
	int status;
	if (!strcmp(action, "on")) status = set_index_enabled(idx, true);
	else if (!strcmp(action, "off")) status = set_index_enabled(idx, false);
	else if (!strcmp(action, "toggle")) status = toggle_index(idx);
	else
	{
		printf("MiSTer_cmd cheat: unknown command '%s'\n", action);
		return;
	}

	printf("MiSTer_cmd cheat %s '%s': %s\n", action, name, cheat_cmd_status(status));
}
