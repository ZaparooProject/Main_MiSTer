#include "save.h"
#include "confstr.h"
#include "kiosk.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "file_io.h"
#include "fpga_io.h"
#include "hardware.h"
#include "menu.h"
#include "osd.h"
#include "spi.h"
#include "user_io.h"
#include "support/arcade/mra_loader.h"
#include "support/n64/n64.h"

// OSD_STATUS crosses into the core's clock domain, so its save FSM needs a few
// frames to start. Then hold until the sector writes stop, capped so a chatty
// core can never wedge the machine.
#define ZSAVE_MIN_HOLD_MS    200
#define ZSAVE_ARCADE_MIN_MS 1200
#define ZSAVE_QUIET_MS       400
#define ZSAVE_MAX_HOLD_MS   2500
#define ZSAVE_REPORT_MS     1200

enum zsave_state { ZSAVE_IDLE, ZSAVE_ARMED, ZSAVE_HOLD, ZSAVE_REPORT };

static zsave_state s_state = ZSAVE_IDLE;
static unsigned long s_min_deadline = 0;
static unsigned long s_quiet_deadline = 0;
static unsigned long s_hold_cap = 0;
static unsigned long s_report_deadline = 0;
static int s_writes_hold = 0;
static int s_writes_after = 0;
static bool s_deferred_load = false;
static bool s_reissuing = false;
static bool s_direct = false;
static bool s_arcade_saved = false;
static bool s_arcade_can_request = false;
static char s_autosave_opt[128];
static int s_autosave_ex = 0;
static uint32_t s_autosave_on = 0;
static uint32_t s_autosave_prev = 0;
static bool s_autosave_restore = false;
static char s_rbf[1024];
static char s_cfg[1024];
static char s_xml[1024];

// Paint before enabling: OsdEnable makes the panel visible with whatever is
// already in the buffer, which would flash stale menu content.
static void paint_banner(const char *text)
{
	char s[40];
	snprintf(s, sizeof(s), "          %s", text);
	OsdSetSize(16);
	OsdSetTitle("Zaparoo", 0);
	for (int i = 0; i < 16; i++) OsdWrite(i, (i == 8) ? s : "");
	OsdUpdate();
}

// The only call that raises OSD_STATUS for generic cores and osd_is_visible,
// which is the edge N64 64DD keys off, so one call covers both.
static void raise_signal(void)
{
	OsdEnable(DISABLE_KEYBOARD);
}

static void lower_signal(void)
{
	OsdDisable();
}

// Arcade hiscore saving is not instant and must be polled. In the shared
// rtl/nvram.v the OSD_STATUS rising edge only *starts* an extraction, which
// pauses the CPU and walks the score region through a timed state machine;
// ioctl_upload_req is raised at the end of that, and only if the scores
// changed, are non-zero, and the core's own autosave option is on. So a single
// check right after raising the signal always reads 0. menu.cpp gets away with
// checking once because it only ever runs with the OSD already long open.
static void arcade_poll_flush(void)
{
	if (!is_arcade() || s_arcade_saved) return;
	uint16_t req = spi_uio_cmd(UIO_CHK_UPLOAD);
	if (!req) return;
	// The core names the index it wants uploaded in the high byte, but
	// arcade_nvm_save() always uses the one the MRA declared. They agree on
	// every core checked; log it rather than silently upload the wrong region.
	printf("zaparoo_save: arcade nvram pending (core index=%u), writing\n", req >> 8);
	arcade_nvm_save();
	s_arcade_saved = true;
}

// Some arcade cores never drive ioctl_upload_req at all, so the flag above can
// never fire for them: the whole JOTEGO family is like this, and so is a Cave
// core older than the one that added its Save NVRAM row. They still answer an
// upload that Main initiates, which is exactly what menu.cpp:3147 does with no
// flag check when the user picks "Save settings".
//
// Restricted to cores that expose no way to ask for a save, because a core with
// a real dump buffer (the shared rtl/nvram.v) only fills it during an
// extraction, so soliciting one unprompted could write stale or blank contents
// over a good file.
static void arcade_blind_flush(void)
{
	if (!is_arcade() || s_arcade_saved || s_arcade_can_request) return;
	printf("zaparoo_save: core cannot request nvram saves, writing unconditionally\n");
	arcade_nvm_save();
	s_arcade_saved = true;
}

// Most cores ship their own Autosave option Off, and with it Off they ignore
// the OSD_STATUS edge entirely, so a forced save writes nothing. Turn it on for
// the duration of the flush and put it back, so the user's core settings are
// unchanged. Not persisted: user_io_status_set alone does not write the .cfg.
static void autosave_override_begin(void)
{
	s_autosave_restore = false;
	if (!zaparoo_confstr_autosave(s_autosave_opt, sizeof(s_autosave_opt),
	                              &s_autosave_ex, &s_autosave_on)) return;

	s_autosave_prev = user_io_status_get(s_autosave_opt, s_autosave_ex);
	if (s_autosave_prev == s_autosave_on) return;

	user_io_status_set(s_autosave_opt, s_autosave_on, s_autosave_ex);
	s_autosave_restore = true;
	printf("zaparoo_save: core autosave was %u, forcing %u for the flush\n",
	            s_autosave_prev, s_autosave_on);
}

static void autosave_override_end(void)
{
	if (!s_autosave_restore) return;
	user_io_status_set(s_autosave_opt, s_autosave_prev, s_autosave_ex);
	s_autosave_restore = false;
}

// Every core that exposes a "Save Backup RAM" row reads it as
// bk_save = <that bit> | <autosave path>, and its save FSM triggers on the
// rising edge of bk_save alone. So one pulse of the bit dumps the save with no
// OSD_STATUS edge, no pending-change flag and no Autosave option involved. Same
// three SPI writes menu.cpp does when the user picks the row, minus the menu,
// which is what makes this path invisible.
static bool direct_save(void)
{
	char opt[128];
	int ex = 0;
	bool usable = false;
	if (!zaparoo_confstr_save_row(opt, sizeof(opt), &ex, &usable))
	{
		printf("zaparoo_save: no save row on this core, falling back to the OSD_STATUS edge\n");
		return false;
	}
	if (!usable)
	{
		// The core is telling us the row would do nothing: no save mounted, or
		// no battery on this cart, or its own Autosave already covers it.
		printf("zaparoo_save: save row %s not live, falling back to the OSD_STATUS edge\n", opt);
		return false;
	}

	user_io_status_set(opt, 1, ex);
	if (is_n64() && !ex && !strcmp(opt, "[41]")) n64_save_dd_disk();
	user_io_status_set(opt, 0, ex);
	printf("zaparoo_save: pulsed core save row %s\n", opt);
	return true;
}

static void enter_hold(void)
{
	s_arcade_saved = false;
	s_arcade_can_request = false;
	s_direct = direct_save();

	// A core can ask Main for a save if it has an explicit row to pulse or an
	// autosave option to turn on. One with neither never drives
	// ioctl_upload_req, so it needs the unconditional path instead.
	if (is_arcade())
	{
		char opt[128];
		int ex = 0;
		uint32_t on = 0;
		s_arcade_can_request = s_direct ||
		                       zaparoo_confstr_autosave(opt, sizeof(opt), &ex, &on);
	}

	if (!s_direct)
	{
		paint_banner("Saving...");
		autosave_override_begin();
		raise_signal();
	}

	s_writes_hold = 0;
	// Arcade extraction runs on its own timers well past the point a generic
	// core has gone quiet, so it gets a longer floor. A core that cannot request
	// a save has nothing to wait for.
	bool slow = is_arcade() && s_arcade_can_request;
	s_min_deadline = GetTimer(slow ? ZSAVE_ARCADE_MIN_MS : ZSAVE_MIN_HOLD_MS);
	s_quiet_deadline = GetTimer(ZSAVE_QUIET_MS);
	s_hold_cap = GetTimer(ZSAVE_MAX_HOLD_MS);
	s_state = ZSAVE_HOLD;
}

// Re-issue a core load that was held back for a flush. Runs from zaparoo_poll
// on a clean stack, so it cannot recurse into user_io_poll the way an inline
// wait inside fpga_load_rbf would.
static void reissue_load(void)
{
	s_deferred_load = false;
	s_state = ZSAVE_IDLE;
	printf("zaparoo_save: flush done, resuming load of %s\n", s_rbf);

	s_reissuing = true;
	fpga_load_rbf(s_rbf, s_cfg[0] ? s_cfg : 0, s_xml[0] ? s_xml : 0);
	// Only reached when the load failed (a missing RBF returns -1); normally
	// fpga_load_rbf ends in app_restart and never comes back.
	s_reissuing = false;
}

static void enter_report(bool release)
{
	arcade_blind_flush();

	// The direct path never raised the signal or touched the Autosave option,
	// so there is nothing to unwind.
	if (s_deferred_load)
	{
		// No point reporting "Saved" a frame before the FPGA is reconfigured.
		if (release && !s_direct) lower_signal();
		if (!s_direct) autosave_override_end();
		reissue_load();
		return;
	}

	if (release)
	{
		if (!s_direct)
		{
			// Lower first so the dump finishes with autosave still on.
			lower_signal();
			autosave_override_end();
		}
		Info("Saved", ZSAVE_REPORT_MS);
	}
	else if (!s_direct)
	{
		autosave_override_end();
	}

	s_writes_after = 0;
	s_report_deadline = GetTimer(ZSAVE_REPORT_MS);
	s_state = ZSAVE_REPORT;
}

bool zaparoo_save_defer_core_load(const char *rbf, const char *cfg, const char *xml)
{
	if (s_reissuing)
	{
		s_reissuing = false;
		return false;
	}
	if (!zaparoo_settings_save_on_exit()) return false;
	if (!rbf || is_menu()) return false;

	// No "does this core have a save" gate. Whether a core holds data it will
	// only write on OSD_STATUS is not reliably knowable from here: PSX drives
	// its own CD outside sd_image[] and mounts its memory card only on a game
	// change, so an inspection-based gate skips it silently. Guessing wrong
	// loses data, so when the user has opted in, always flush. The cost is the
	// half second the confirmation page warns about.
	snprintf(s_rbf, sizeof(s_rbf), "%s", rbf);
	snprintf(s_cfg, sizeof(s_cfg), "%s", cfg ? cfg : "");
	snprintf(s_xml, sizeof(s_xml), "%s", xml ? xml : "");
	s_deferred_load = true;

	// A save already running just adopts the new target when it reports.
	if (s_state == ZSAVE_IDLE)
	{
		s_writes_hold = 0;
		s_writes_after = 0;
		s_state = ZSAVE_ARMED;
	}

	printf("zaparoo_save: holding core load for a flush\n");
	return true;
}

bool zaparoo_save_request(void)
{
	if (is_menu())
	{
		printf("zaparoo_save: no core loaded, nothing to save\n");
		return false;
	}
	if (s_state != ZSAVE_IDLE)
	{
		printf("zaparoo_save: already in progress\n");
		return false;
	}
	if (menu_present())
	{
		// The OSD is open, so OSD_STATUS is already high and the core's dump
		// happened when it opened. Only the arcade half is left, and lowering
		// the signal here would close the user's menu.
		s_arcade_saved = false;
		arcade_poll_flush();
		printf("zaparoo_save: OSD already open, arcade NVRAM only\n");
		return true;
	}

	s_writes_hold = 0;
	s_writes_after = 0;
	// Never touch SPI on the requester's stack: a request can arrive from the
	// command FIFO or from deep inside a core load.
	s_state = ZSAVE_ARMED;
	return true;
}

void zaparoo_save_note_write(void)
{
	if (s_state == ZSAVE_HOLD)
	{
		s_writes_hold++;
		s_quiet_deadline = GetTimer(ZSAVE_QUIET_MS);
	}
	else if (s_state == ZSAVE_REPORT)
	{
		s_writes_after++;
	}
}

void zaparoo_poll(void)
{
	zaparoo_kiosk_poll();

	switch (s_state)
	{
	case ZSAVE_IDLE:
		break;

	case ZSAVE_ARMED:
		enter_hold();
		break;

	case ZSAVE_HOLD:
		// Both bail-outs are about ownership of OSD_STATUS, which the direct
		// path never takes, so they only apply to the fallback.
		if (!s_direct)
		{
			// Signal lost first: Info()/InfoMessage() from elsewhere send a
			// command with OSD_INFO or OSD_MSG set, which drives OSD_STATUS low.
			if (!user_io_osd_is_visible())
			{
				printf("zaparoo_save: signal lost mid-hold (writes=%d)\n", s_writes_hold);
				enter_report(false);
				break;
			}
			if (menu_present())
			{
				// The user opened the real OSD over us; it owns the signal now,
				// so do not lower it. A held-back core load still has to happen.
				printf("zaparoo_save: OSD opened during save, releasing\n");
				if (s_deferred_load)
				{
					reissue_load();
					break;
				}
				s_state = ZSAVE_IDLE;
				break;
			}
		}
		arcade_poll_flush();
		if (CheckTimer(s_hold_cap))
		{
			printf("zaparoo_save: hold cap reached (writes=%d, arcade_saved=%d)\n",
			            s_writes_hold, s_arcade_saved);
			enter_report(true);
			break;
		}
		// Arcade has no sector writes to go quiet, so its own flush ends the hold.
		if (s_arcade_saved) enter_report(true);
		else if (CheckTimer(s_min_deadline) && CheckTimer(s_quiet_deadline)) enter_report(true);
		break;

	case ZSAVE_REPORT:
		if (!CheckTimer(s_report_deadline)) break;
		printf("zaparoo_save: done (writes during hold=%d, after release=%d)\n",
		            s_writes_hold, s_writes_after);
		s_state = ZSAVE_IDLE;
		break;

	default:
		s_state = ZSAVE_IDLE;
		break;
	}
}
