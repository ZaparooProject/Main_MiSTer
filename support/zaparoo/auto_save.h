#pragma once

void auto_save_poll(void);
void auto_save_on_save_mounted(unsigned char index, const char *path);
void auto_save_on_save_unmounted(unsigned char index);
void auto_save_on_sector_write(int disk);
