#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <inttypes.h>

#include "cheats.h"
#include "cheat_cmd.h"

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
	case CHEATS_CMD_OK: return "ok";
	case CHEATS_CMD_NO_CHEATS: return "no cheats loaded";
	case CHEATS_CMD_NOT_FOUND: return "not found";
	case CHEATS_CMD_LOAD_FAILED: return "load failed";
	case CHEATS_CMD_NO_ROOM: return "no room";
	default: return "error";
	}
}

void cheat_cmd(const char *cmd)
{
	char action[16];

	if (!parse_token(&cmd, action, sizeof(action)))
	{
		printf("MiSTer_cmd cheat: missing command\n");
		return;
	}

	if (!strcmp(action, "clear"))
	{
		int status = cheats_clear_enabled();
		printf("MiSTer_cmd cheat clear: %s\n", cheat_cmd_status(status));
		return;
	}

	const char *name = skip_space(cmd);
	if (!*name)
	{
		printf("MiSTer_cmd cheat %s: missing name\n", action);
		return;
	}

	int status;
	if (!strcmp(action, "on")) status = cheats_set_enabled_by_name(name, true);
	else if (!strcmp(action, "off")) status = cheats_set_enabled_by_name(name, false);
	else if (!strcmp(action, "toggle")) status = cheats_toggle_by_name(name);
	else
	{
		printf("MiSTer_cmd cheat: unknown command '%s'\n", action);
		return;
	}

	printf("MiSTer_cmd cheat %s '%s': %s\n", action, name, cheat_cmd_status(status));
}
