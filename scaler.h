/*
Copyright 2019 alanswx
with help from the MiSTer contributors including Grabulosaure
*/

#ifndef SCALER_H
#define SCALER_H

#include <stdint.h>

typedef enum {
    RGB,
    BGR,
    BGRA,
    RGBA,
    ARGB32, // respect endianness
    YUV,
} mister_scaler_format_t;

typedef struct {
   int header;
   int width;
   int height;
   int line;
   int output_width;
   int output_height;

   char *map;
   int num_bytes;
   int map_off;
} mister_scaler;

#define MISTER_SCALER_BASEADDR     0x20000000
#define MISTER_SCALER_BUFFERSIZE   2048*3*1024

mister_scaler *mister_scaler_init();
int mister_scaler_read(mister_scaler *,unsigned char *buffer, mister_scaler_format_t format = ARGB32);
void mister_scaler_free(mister_scaler *);
bool write_screenshot(const char *filename, const uint8_t *pixels, int width, int height,
                      int output_width = 0, int output_height = 0);

void request_screenshot(char *cmd, int scaled = 0);
void screenshot_cb(void);

#endif
