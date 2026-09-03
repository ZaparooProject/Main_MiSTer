#pragma once

#include <stdbool.h>
#include <stdint.h>

// Native CRT settings shared with the Zaparoo frontend. The frontend owns
// these normally; this side duplicates just enough for the OSD fallback page
// (support/zaparoo/launcher_pages.cpp) so a user whose frontend cannot show
// a picture on the CRT can still fix the standard and centering.
//
// Video standard ids are the DDR word1 mode nibble and byte 1 of
// zaparoo_launcher_crt.bin: 0 NTSC 352x240p60, 1 480i 720x480i60,
// 2 PAL 352x288p50. The frontend persists the standard and the H/V
// centering trims under [settings] in zaparoo/frontend.toml and treats that
// file as authoritative on its next start.

#define CRT_STD_NTSC 0
#define CRT_STD_480I 1
#define CRT_STD_PAL  2

#define CRT_H_OFFSET_MIN -8
#define CRT_H_OFFSET_MAX  8
#define CRT_V_OFFSET_MIN -8
#define CRT_V_OFFSET_MAX  2

const char *crt_standard_name(uint8_t mode);
uint8_t crt_standard_next(uint8_t mode);

// frontend.toml [settings] mirror. Writes edit one line in place and keep
// everything else in the file untouched.
bool crt_toml_set_standard(uint8_t mode);
void crt_toml_get_offsets(int *h, int *v);
bool crt_toml_set_offsets(int h, int v);

// Live centering: rewrites DDR control word1. Only meaningful while the
// menu core is scanning a published frame (frontend --crt or the test
// pattern below).
void crt_offsets_apply_live(int h, int v, uint8_t mode);

// Main-drawn alignment pattern published into DDR slot 0 so centering can
// be adjusted without a running frontend. unpublish returns the core to
// its default pattern.
bool crt_test_pattern_publish(uint8_t mode, int h, int v);
void crt_test_pattern_unpublish(void);
bool crt_test_pattern_active(void);
