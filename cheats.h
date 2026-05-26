#ifndef CHEATS_H
#define CHEATS_H

void cheats_init(const char *rom_path, uint32_t romcrc);
int cheats_available();
void cheats_scan(int mode);
void cheats_scroll_name();
void cheats_print();
void cheats_toggle();
int cheats_loaded();

enum
{
	CHEATS_CMD_OK = 0,
	CHEATS_CMD_NO_CHEATS = -1,
	CHEATS_CMD_NOT_FOUND = -2,
	CHEATS_CMD_LOAD_FAILED = -3,
	CHEATS_CMD_NO_ROOM = -4
};

int cheats_set_enabled_by_name(const char *name, bool enabled);
int cheats_toggle_by_name(const char *name);
int cheats_clear_enabled();

void cheats_init_arcade(int unit_size, int max_active);
void cheats_add_arcade(const char *name, const char *cheatData, int cheatSize);
void cheats_finalize_arcade();

#endif
