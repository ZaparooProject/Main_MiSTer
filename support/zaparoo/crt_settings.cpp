#include "crt_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file_io.h"
#include "shmem.h"

static const char s_toml_rel[] = "zaparoo/frontend.toml";
static const uint32_t s_native_addr = 0x3A000000u;
static const uint32_t s_native_size = 0x00300000u;
static const uint32_t s_slot0_off = 0x1000u;
static const uint32_t s_word1_magic = 0x5A50u;
static const size_t s_toml_max = 65536;

static bool s_pattern_active = false;

const char *crt_standard_name(uint8_t mode)
{
	switch (mode)
	{
	case CRT_STD_PAL: return "PAL";
	case CRT_STD_480I: return "480i";
	default: return "NTSC";
	}
}

uint8_t crt_standard_next(uint8_t mode)
{
	// 480i is not offered, matching the frontend's picker.
	return mode == CRT_STD_PAL ? CRT_STD_NTSC : CRT_STD_PAL;
}

static int clamp_int(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

// ---- frontend.toml [settings] line editor ----------------------------------

static char *toml_read(size_t *len)
{
	*len = 0;
	char *buf = (char *)malloc(s_toml_max + 1);
	if (!buf) return NULL;
	buf[0] = 0;
	FILE *f = fopen(getFullPath(s_toml_rel), "rb");
	if (f)
	{
		*len = fread(buf, 1, s_toml_max, f);
		fclose(f);
		buf[*len] = 0;
		if (memchr(buf, 0, *len))
		{
			// Not a text file we understand; refuse to rewrite it.
			free(buf);
			return NULL;
		}
	}
	return buf;
}

static bool toml_write(const char *data, size_t len)
{
	char path[1100], tmp[1120];
	snprintf(path, sizeof(path), "%s", getFullPath(s_toml_rel));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, len, f) == len;
	if (fclose(f)) ok = false;
	if (ok && rename(tmp, path)) ok = false;
	if (!ok) unlink(tmp);
	return ok;
}

// Start of the first line after "[name]" and, via *end, the start of the next
// section header (or end of buffer). NULL when the section is absent.
static char *toml_find_section(char *buf, const char *name, char **end)
{
	size_t nlen = strlen(name);
	char *found = NULL;
	char *p = buf;
	while (*p)
	{
		char *nl = strchr(p, '\n');
		char *next = nl ? nl + 1 : p + strlen(p);
		char *s = p;
		while (*s == ' ' || *s == '\t') s++;
		if (*s == '[')
		{
			if (found)
			{
				*end = p;
				return found;
			}
			if (!strncmp(s + 1, name, nlen) && s[1 + nlen] == ']') found = next;
		}
		p = next;
	}
	if (found) *end = p;
	return found;
}

// "key = ..." line inside [start, end); *line_end is the start of the following line.
static char *toml_find_key(char *start, char *end, const char *key, char **line_end)
{
	size_t klen = strlen(key);
	char *p = start;
	while (p < end)
	{
		char *nl = (char *)memchr(p, '\n', end - p);
		char *next = nl ? nl + 1 : end;
		char *s = p;
		while (*s == ' ' || *s == '\t') s++;
		if (!strncmp(s, key, klen))
		{
			char *q = s + klen;
			while (*q == ' ' || *q == '\t') q++;
			if (*q == '=')
			{
				*line_end = next;
				return p;
			}
		}
		p = next;
	}
	return NULL;
}

static bool toml_set(const char *key, const char *value)
{
	size_t len;
	char *buf = toml_read(&len);
	if (!buf) return false;

	char line[128];
	int llen = snprintf(line, sizeof(line), "%s = %s\n", key, value);
	char *out = (char *)malloc(len + llen + 32);
	if (!out)
	{
		free(buf);
		return false;
	}

	size_t n = 0;
	char *sec_end = NULL;
	char *sec = toml_find_section(buf, "settings", &sec_end);
	if (!sec)
	{
		memcpy(out, buf, len);
		n = len;
		if (n && out[n - 1] != '\n') out[n++] = '\n';
		n += sprintf(out + n, "\n[settings]\n%s", line);
	}
	else
	{
		char *kend = NULL;
		char *k = toml_find_key(sec, sec_end, key, &kend);
		char *at = k ? k : sec;
		char *rest = k ? kend : sec;
		memcpy(out, buf, at - buf);
		n = at - buf;
		memcpy(out + n, line, llen);
		n += llen;
		memcpy(out + n, rest, len - (rest - buf));
		n += len - (rest - buf);
	}

	bool ok = toml_write(out, n);
	if (!ok) printf("crt_settings: unable to update %s\n", s_toml_rel);
	free(out);
	free(buf);
	return ok;
}

static bool toml_get_int(const char *key, int *val)
{
	size_t len;
	char *buf = toml_read(&len);
	if (!buf) return false;
	bool ok = false;
	char *sec_end = NULL;
	char *sec = toml_find_section(buf, "settings", &sec_end);
	if (sec)
	{
		char *kend = NULL;
		char *k = toml_find_key(sec, sec_end, key, &kend);
		if (k)
		{
			char *eq = strchr(k, '=');
			if (eq && eq < kend)
			{
				char *endp = NULL;
				long v = strtol(eq + 1, &endp, 10);
				if (endp != eq + 1)
				{
					*val = (int)v;
					ok = true;
				}
			}
		}
	}
	free(buf);
	return ok;
}

bool crt_toml_set_standard(uint8_t mode)
{
	const char *v = mode == CRT_STD_PAL ? "\"pal\"" : mode == CRT_STD_480I ? "\"480i\"" : "\"ntsc\"";
	return toml_set("crt_video_standard", v);
}

void crt_toml_get_offsets(int *h, int *v)
{
	int hv = 0, vv = 0;
	toml_get_int("crt_h_offset", &hv);
	toml_get_int("crt_v_offset", &vv);
	*h = clamp_int(hv, CRT_H_OFFSET_MIN, CRT_H_OFFSET_MAX);
	*v = clamp_int(vv, CRT_V_OFFSET_MIN, CRT_V_OFFSET_MAX);
}

bool crt_toml_set_offsets(int h, int v)
{
	char s[16];
	snprintf(s, sizeof(s), "%d", clamp_int(h, CRT_H_OFFSET_MIN, CRT_H_OFFSET_MAX));
	bool ok = toml_set("crt_h_offset", s);
	snprintf(s, sizeof(s), "%d", clamp_int(v, CRT_V_OFFSET_MIN, CRT_V_OFFSET_MAX));
	return toml_set("crt_v_offset", s) && ok;
}

// ---- DDR control words / test pattern --------------------------------------

// word1: [31:16] magic, [15:8] h as int8 (+ right), [7:4] v as int4 (+ down), [3:0] mode.
static uint32_t pack_word1(int h, int v, uint8_t mode)
{
	uint32_t hb = (uint8_t)(int8_t)clamp_int(h, CRT_H_OFFSET_MIN, CRT_H_OFFSET_MAX);
	uint32_t vb = ((uint32_t)(int8_t)clamp_int(v, CRT_V_OFFSET_MIN, CRT_V_OFFSET_MAX)) & 0xFu;
	return (s_word1_magic << 16) | (hb << 8) | (vb << 4) | (mode & 0xFu);
}

void crt_offsets_apply_live(int h, int v, uint8_t mode)
{
	void *p = shmem_map(s_native_addr, 0x1000);
	if (!p) return;
	volatile uint32_t *w = (volatile uint32_t *)p;
	w[1] = pack_word1(h, v, mode);
	__sync_synchronize();
	shmem_unmap(p, 0x1000);
}

static void mode_dims(uint8_t mode, int *w, int *h)
{
	switch (mode)
	{
	case CRT_STD_480I: *w = 720; *h = 480; break;
	case CRT_STD_PAL: *w = 352; *h = 288; break;
	default: *w = 352; *h = 240; break;
	}
}

static void hline(uint32_t *px, int w, int h, int y, int x0, int x1, uint32_t c)
{
	if (y < 0 || y >= h) return;
	for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) px[y * w + x] = c;
}

static void vline(uint32_t *px, int w, int h, int x, int y0, int y1, uint32_t c)
{
	if (x < 0 || x >= w) return;
	for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++) px[y * w + x] = c;
}

bool crt_test_pattern_publish(uint8_t mode, int h, int v)
{
	int w, ht;
	mode_dims(mode, &w, &ht);
	void *p = shmem_map(s_native_addr, s_native_size);
	if (!p)
	{
		printf("crt_settings: shmem_map(0x%x) failed\n", s_native_addr);
		return false;
	}
	volatile uint32_t *words = (volatile uint32_t *)p;
	uint32_t *px = (uint32_t *)((uint8_t *)p + s_slot0_off);

	// Core reads 0x00RRGGBB; the top byte is ignored.
	const uint32_t white = 0x00FFFFFFu, grey = 0x00A0A0A0u, dim = 0x00404040u;
	memset(px, 0, (size_t)w * ht * 4);

	// 16x12 crosshatch, edge frame, 5% action-safe box, centre cross.
	for (int i = 1; i < 16; i++) vline(px, w, ht, i * w / 16, 0, ht - 1, dim);
	for (int j = 1; j < 12; j++) hline(px, w, ht, j * ht / 12, 0, w - 1, dim);
	int ix = w / 20, iy = ht / 20;
	hline(px, w, ht, iy, ix, w - 1 - ix, grey);
	hline(px, w, ht, ht - 1 - iy, ix, w - 1 - ix, grey);
	vline(px, w, ht, ix, iy, ht - 1 - iy, grey);
	vline(px, w, ht, w - 1 - ix, iy, ht - 1 - iy, grey);
	hline(px, w, ht, 0, 0, w - 1, white);
	hline(px, w, ht, ht - 1, 0, w - 1, white);
	vline(px, w, ht, 0, 0, ht - 1, white);
	vline(px, w, ht, w - 1, 0, ht - 1, white);
	hline(px, w, ht, ht / 2, w / 2 - 16, w / 2 + 16, white);
	vline(px, w, ht, w / 2, ht / 2 - 12, ht / 2 + 12, white);

	// word1 before word0: the core latches both per vblank and a non-zero
	// word0 publishes the frame.
	words[1] = pack_word1(h, v, mode);
	__sync_synchronize();
	words[0] = (1u << 2) | 0u;
	__sync_synchronize();
	shmem_unmap(p, s_native_size);
	s_pattern_active = true;
	printf("crt_settings: test pattern published %dx%d mode=%d\n", w, ht, mode);
	return true;
}

void crt_test_pattern_unpublish(void)
{
	if (!s_pattern_active) return;
	s_pattern_active = false;
	void *p = shmem_map(s_native_addr, 0x1000);
	if (!p) return;
	volatile uint32_t *words = (volatile uint32_t *)p;
	words[0] = 0;
	__sync_synchronize();
	shmem_unmap(p, 0x1000);
}

bool crt_test_pattern_active(void)
{
	return s_pattern_active;
}
