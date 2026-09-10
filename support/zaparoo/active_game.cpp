#include "active_game.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "file_io.h"

static const char s_active_game_file[] = "/tmp/ACTIVEGAME";
// Zaparoo Core and mrext generate this MGL for every load_core they send and
// write the real game path to ACTIVEGAME themselves before sending it. The file
// is rewritten on the next launch, so its path never identifies a game.
static const char s_launcher_temp_mgl[] = "/media/fat/.LASTLAUNCH.mgl";

static void active_game_write(const char *path)
{
	const char *value = (path && path[0]) ? getFullPath(path) : "";
	FILE *file = fopen(s_active_game_file, "w");
	if (!file)
	{
		printf("Failed to write %s: %s\n", s_active_game_file, strerror(errno));
		return;
	}

	size_t len = strlen(value);
	if (len && fwrite(value, 1, len, file) != len)
		printf("Failed to write %s: %s\n", s_active_game_file, strerror(errno));
	if (fclose(file))
		printf("Failed to close %s: %s\n", s_active_game_file, strerror(errno));
}

void zaparoo_active_game_set_core(const char *path)
{
	if (path && isXmlName(path) == 2 && !strcasecmp(getFullPath(path), s_launcher_temp_mgl)) return;
	active_game_write(path && isXmlName(path) ? path : "");
}

void zaparoo_active_game_set_file(const char *dir, const char *path, int recent_idx)
{
	if (!path || !path[0]) return;
	if (recent_idx < 0 && !isXmlName(path)) return;
	// Slot 15 is menu.cpp's video preset slot (MENU_PRESET_FILE_SELECTED).
	if (recent_idx == 15) return;
	if (path[0] == '/')
	{
		active_game_write(path);
		return;
	}

	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	char selected[PATH_MAX];
	int len = (dir && dir[0]) ? snprintf(selected, sizeof(selected), "%s/%s", dir, name)
		: snprintf(selected, sizeof(selected), "%s", path);
	if (len < 0 || len >= (int)sizeof(selected))
	{
		printf("Failed to write %s: selected path is too long\n", s_active_game_file);
		return;
	}
	active_game_write(selected);
}
