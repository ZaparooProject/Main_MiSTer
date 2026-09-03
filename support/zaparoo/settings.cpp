#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "file_io.h"

static const char s_settings_file[] = "zaparoo_settings.bin";

#define IDX_FRONTEND_DISABLED 0
#define IDX_KIOSK             1
#define IDX_SAVE_ON_EXIT      2

static uint8_t s_blob[ZAPAROO_SETTINGS_SIZE];
static bool s_loaded = false;
static char s_root[64] = {};

// Cached: the predicates run per scheduler tick, per gamepad event and inside
// the OSD scroll-render loop, so a file read here is a visible stall. Keyed on
// the storage root because FindStorage() runs a cfg_parse() with the root
// forced to SD while it waits for USB, which would otherwise pin the cache to
// the wrong device for the rest of the session.
static const uint8_t *settings_blob(void)
{
	const char *root = getRootDir();
	if (!s_loaded || strncmp(s_root, root, sizeof(s_root) - 1))
	{
		snprintf(s_root, sizeof(s_root), "%s", root);
		memset(s_blob, 0, sizeof(s_blob));
		FileLoadConfig(s_settings_file, s_blob, sizeof(s_blob));
		s_loaded = true;
	}
	return s_blob;
}

static bool settings_set(int idx, uint8_t value)
{
	settings_blob();
	if (s_blob[idx] == value) return true;

	uint8_t prev = s_blob[idx];
	s_blob[idx] = value;
	if (!FileSaveConfig(s_settings_file, s_blob, sizeof(s_blob)))
	{
		s_blob[idx] = prev;
		printf("zaparoo_settings: could not write %s\n", s_settings_file);
		return false;
	}
	return true;
}

bool zaparoo_settings_frontend_enabled(void)
{
	return settings_blob()[IDX_FRONTEND_DISABLED] == 0;
}

bool zaparoo_settings_set_frontend_enabled(bool enabled)
{
	return settings_set(IDX_FRONTEND_DISABLED, enabled ? 0 : 1);
}

bool zaparoo_settings_kiosk_active(void)
{
	return settings_blob()[IDX_KIOSK] != 0;
}

bool zaparoo_settings_set_kiosk(bool on)
{
	return settings_set(IDX_KIOSK, on ? 1 : 0);
}

bool zaparoo_settings_save_on_exit(void)
{
	return settings_blob()[IDX_SAVE_ON_EXIT] != 0;
}

bool zaparoo_settings_set_save_on_exit(bool on)
{
	return settings_set(IDX_SAVE_ON_EXIT, on ? 1 : 0);
}

void zaparoo_settings_invalidate(void)
{
	s_loaded = false;
}
