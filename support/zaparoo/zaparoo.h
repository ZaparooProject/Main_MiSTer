#pragma once
#ifdef ZAPAROO

#define ZAPAROO_FEATURES_FILE "/tmp/MAINFEATURES"

void zaparoo_publish_features(void);
bool zaparoo_handle_input_cmd(const char *cmd);

#endif
