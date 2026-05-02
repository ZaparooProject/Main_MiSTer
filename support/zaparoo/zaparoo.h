#pragma once

#include <stdint.h>

#define ZAPAROO_FEATURES_FILE "/tmp/MAINFEATURES"

void zaparoo_cfg_defaults(void);
void zaparoo_publish_features(void);
bool zaparoo_handle_input_cmd(const char *cmd);
uint16_t zaparoo_fb_terminal_key(uint32_t mask, bool osd_button);
