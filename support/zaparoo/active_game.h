#pragma once

#include <stddef.h>

bool zaparoo_active_game_read(char *path, size_t capacity);
void zaparoo_active_game_set_core(const char *path);
void zaparoo_active_game_set_file(const char *dir, const char *path, int recent_idx);
