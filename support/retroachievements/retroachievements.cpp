#include "retroachievements.h"
#include "ra_gate.h"
#include "achievements.h"
#include "achievements_console.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input.h>

#include "../../user_io.h"
#include "../../menu.h"
#include "../../osd.h"
#include "../../file_io.h"
#include "../zaparoo/kiosk.h"
#include "../n64/n64.h"

#define RA_MAX_DEV 30

static bool s_checked = false;
static bool s_active = false;
static int s_menu_row = -1;
static uint32_t s_ret_state = 0;
static uint32_t s_ret_sub = 0;
static uint8_t s_combo_y[RA_MAX_DEV] = {};
static uint8_t s_combo_fired[RA_MAX_DEV] = {};
static char s_n64_md5[33] = {};

static void ra_check_init(void)
{
	if (s_checked) return;
	s_checked = true;

	if (!ra_gate_is_ra_core(user_io_get_core_name(0), get_rbf_path())) return;
	if (!get_console_handler_by_name(user_io_get_core_name(1)))
	{
		printf("RA: %s is not a RetroAchievements console\n", user_io_get_core_name(0));
		return;
	}

	printf("RA: enabling RetroAchievements for %s\n", user_io_get_core_name(0));
	achievements_init();
	s_active = true;
}

void ra_poll(void)
{
	ra_check_init();
	if (s_active) achievements_poll();
}

void ra_game_loaded(const char *path, uint32_t crc)
{
	ra_check_init();
	if (s_active) achievements_load_game(path, crc);
}

void ra_core_reset(void)
{
	if (s_active) achievements_notify_core_reset();
}

void ra_status_option_set(int bit, int ex, const char *conf)
{
	if (!s_active) return;

	// Only after the reset pulse: before it, an in-flight FPGA scan can rewrite
	// the mirror header the ARM just cleared. SMS "Eject ROM" (status[9]) leaves
	// the core running on garbage RAM, so it ends the session.
	if (!bit) achievements_notify_core_reset();
	else if (!ex && bit == 9 && !strcasecmp(user_io_get_core_name(1), "SMS")) achievements_unload_game();
	else if (conf)
	{
		char lbl[256];
		substrcpy(lbl, conf, 1);
		if (strcasestr(lbl, "load state")) achievements_notify_state_loaded();
	}
}

void ra_save_io(void)
{
	if (s_active) achievements_notify_save_io();
}

void ra_osd_enable(unsigned char mode)
{
	// Autosave cores stream save RAM as soon as the OSD opens, so the guard is
	// armed first. Message windows never raise the core's OSD status.
	if (s_active && !(mode & OSD_MSG)) achievements_notify_save_io();
}

bool ra_cheats_blocked(bool notify)
{
	if (!s_active || !achievements_hardcore_active()) return false;
	if (notify) Info("Hardcore Mode: cheats disabled", 3000);
	return true;
}

int ra_osd_event_filter(int dev, int ev_type, int code, int value, uint32_t y_code, int osd_event, int mapping)
{
	if (!s_active || dev < 0 || dev >= RA_MAX_DEV) return osd_event;

	if (ev_type == EV_KEY && y_code && (uint32_t)code == y_code) s_combo_y[dev] = value ? 1 : 0;
	if (!achievements_list_hotkey_enabled()) return osd_event;

	if (osd_event == 1 && !mapping && !user_io_osd_is_visible() && s_combo_y[dev]
		&& achievements_has_active_game() && !zaparoo_kiosk_active())
	{
		s_combo_fired[dev] = 1;
		menu_key_set(KEY_F6);
		return 0;
	}

	if (osd_event == 2 && s_combo_fired[dev])
	{
		s_combo_fired[dev] = 0;
		return 0;
	}

	return osd_event;
}

bool ra_menu_add_row(int page, int row)
{
	s_menu_row = (s_active && !page && achievements_has_active_game()) ? row : -1;
	return s_menu_row >= 0;
}

bool ra_menu_row_selected(uint32_t menusub, int select)
{
	return select && s_menu_row >= 0 && menusub == (uint32_t)s_menu_row;
}

bool ra_list_open(uint32_t ret_state, uint32_t ret_sub)
{
	if (!s_active || !achievements_has_active_game() || zaparoo_kiosk_active()) return false;
	if (achievements_list_open() <= 0) return false;

	printf("RA: achievement list opened (return state %u)\n", ret_state);
	s_ret_state = ret_state;
	s_ret_sub = ret_sub;
	OsdSetSize(16);
	OsdClear();
	OsdEnable(DISABLE_KEYBOARD);
	return true;
}

void ra_list_render(void)
{
	char title[32];
	snprintf(title, sizeof(title), "Achievements (%d)", achievements_list_count());
	OsdSetTitle(title);
	achievements_list_print();
}

int ra_list_input(int back, int select, int up, int down, uint32_t key, uint32_t *ret_state, uint32_t *ret_sub)
{
	achievements_list_ticker();

	if (back)
	{
		achievements_list_close();
		*ret_state = s_ret_state;
		*ret_sub = s_ret_sub;
		return RA_LIST_CLOSE;
	}

	if ((select || key == KEY_TAB) && achievements_list_selected_is_ach()) return RA_LIST_DETAIL;

	switch (key)
	{
	case KEY_HOME:     achievements_list_scan(SCANF_INIT); return RA_LIST_REDRAW;
	case KEY_END:      achievements_list_scan(SCANF_END); return RA_LIST_REDRAW;
	case KEY_PAGEUP:   achievements_list_scan(SCANF_PREV_PAGE); return RA_LIST_REDRAW;
	case KEY_PAGEDOWN: achievements_list_scan(SCANF_NEXT_PAGE); return RA_LIST_REDRAW;
	// Left/Right switch sets on multiset games, else they page.
	case KEY_LEFT:
		if (!achievements_list_switch_set(-1)) achievements_list_scan(SCANF_PREV_PAGE);
		return RA_LIST_REDRAW;
	case KEY_RIGHT:
		if (!achievements_list_switch_set(1)) achievements_list_scan(SCANF_NEXT_PAGE);
		return RA_LIST_REDRAW;
	}

	if (down)
	{
		achievements_list_scan(SCANF_NEXT);
		return RA_LIST_REDRAW;
	}

	if (up)
	{
		achievements_list_scan(SCANF_PREV);
		return RA_LIST_REDRAW;
	}

	return RA_LIST_NONE;
}

void ra_detail_render(void)
{
	OsdSetTitle("Achievement");
	achievements_detail_print();
}

void ra_n64_set_rom_md5(const char *md5_hex)
{
	snprintf(s_n64_md5, sizeof(s_n64_md5), "%s", md5_hex ? md5_hex : "");
}

const char *n64_get_rom_md5()
{
	return s_n64_md5;
}

bool ra_skip_main_restart(const char *exe)
{
	if (!ra_gate_is_ra_main(exe)) return false;
	printf("RA: RetroAchievements is built in, staying on this Main instead of %s\n", exe);
	return true;
}

static bool ra_bundle_usable(const char *path)
{
	struct stat st;
	return !stat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && !access(path, R_OK);
}

const char *ra_ca_bundle_path(void)
{
	// Same order as Zaparoo Core: Update All's bundle is kept current, the
	// system one ships with the image.
	static const char *paths[] = {
		"/media/fat/Scripts/.config/downloader/cacert.pem",
		"/etc/ssl/certs/cacert.pem",
	};

	for (const char *path : paths)
	{
		if (ra_bundle_usable(path)) return path;
	}
	return NULL;
}
