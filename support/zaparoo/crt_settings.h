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

// Analog H size (width stretch), steps of 1/64 pixel period. 0 = unity
// (retimer bypassed). Positive limit +2: the retimer drains each line
// before the incoming HSYNC and NTSC's 12 px front porch caps the growth.
#define CRT_H_SIZE_MIN -8
#define CRT_H_SIZE_MAX  2

const char *crt_standard_name(uint8_t mode);
uint8_t crt_standard_next(uint8_t mode);

// frontend.toml [settings] mirror. Writes edit one line in place and keep
// everything else in the file untouched.
bool crt_toml_set_standard(uint8_t mode);
void crt_toml_get_offsets(int *h, int *v);
bool crt_toml_set_offsets(int h, int v);
void crt_toml_get_hsize(int *s);
bool crt_toml_set_hsize(int s);

// Live centering: rewrites DDR control word1. Only meaningful while the
// menu core is scanning a published frame (frontend --crt or the test
// pattern below).
void crt_offsets_apply_live(int h, int v, uint8_t mode);

// Interim word2 contract (analog H size). The core's per-vblank control
// poll reads two 64-bit beats at 0x3A000000: beat 1 is the unchanged v2
// word0/word1 (magics 0x5A50/0x5A51); beat 2's low word ("word2", bytes
// 0x08-0x0B) is [31:16] magic 0x5A52, [15:8] reserved 0, [7:0] signed
// h_size (core-clamped to -8..+2; anything without the magic reads as 0).
// Word2 ownership matches word1's offsets: a running frontend owns it (it
// writes the saved h_size at arm and on live calibration nudges, v byte 0),
// and Main writes it from this page's live apply, the test pattern, and a
// republish after alt_launcher's pre-spawn 3 MB blank. Main's OSD save
// respawns the frontend so the two writers never run stale side by side.
// h_size applies only while word1's magic is valid. The deferred v3 plan
// (plans/menu-crt-video-plan.md sec. 7) repurposes byte 0x08 and moves
// 0x5A52 into word1; matched releases will replace this layout wholesale.
void crt_hsize_apply_live(int s);
void crt_hsize_republish(void);

// Main-drawn alignment pattern published into DDR slot 0 so centering can
// be adjusted without a running frontend. unpublish returns the core to
// its default pattern.
bool crt_test_pattern_publish(uint8_t mode, int h, int v);
void crt_test_pattern_unpublish(void);
bool crt_test_pattern_active(void);
