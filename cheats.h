#ifndef CHEATS_H
#define CHEATS_H

void cheats_init(const char *rom_path, uint32_t romcrc);
int cheats_available();
void cheats_scan(int mode);
void cheats_scroll_name();
void cheats_print();
void cheats_toggle();
int cheats_loaded();

const char *cheats_get_name(int idx);
bool cheats_get_enabled(int idx);
int cheats_get_selected();
void cheats_set_selected(int idx);

void cheats_init_arcade(int unit_size, int max_active);
void cheats_add_arcade(const char *name, const char *cheatData, int cheatSize);
void cheats_finalize_arcade();

#endif
