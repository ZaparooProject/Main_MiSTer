#pragma once

#include <stdint.h>

// The regular OSD owns the gameplay, input and lock checks.
bool zaparoo_artwork_can_open();
bool zaparoo_artwork_menu_button(int player, bool pressed);

// Runs on the first screenshot frame callback after the Menu release.
void zaparoo_artwork_capture_frame();

bool zaparoo_artwork_menu_poll();
bool zaparoo_artwork_menu_update(uint32_t key); // true when the page should close
void zaparoo_artwork_menu_close();
