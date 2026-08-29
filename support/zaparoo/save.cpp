#include "save.h"
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

// OSD_STATUS crosses into the core's clock domain, so its save FSM needs a few
// frames to start. Then hold until the sector writes stop, capped so a chatty
// core can never wedge the machine.
#define ZSAVE_MIN_HOLD_MS 200
#define ZSAVE_QUIET_MS    400
#define ZSAVE_MAX_HOLD_MS 2500
// A held signal has no rising edge, so saving while paused must dip first.
#define ZSAVE_DIP_MS      50
#define ZSAVE_REPORT_MS   1200

enum zsave_state { ZSAVE_IDLE, ZSAVE_ARMED, ZSAVE_DIP, ZSAVE_HOLD, ZSAVE_REPORT };
enum zosd_owner { ZOSD_NONE, ZOSD_SAVE, ZOSD_PAUSE };

static zsave_state s_state = ZSAVE_IDLE;
static zosd_owner s_owner = ZOSD_NONE;
static unsigned long s_dip_deadline = 0;
static unsigned long s_min_deadline = 0;
static unsigned long s_quiet_deadline = 0;
static unsigned long s_hold_cap = 0;
static unsigned long s_report_deadline = 0;
static int s_writes_hold = 0;
static int s_writes_after = 0;
static bool s_resume_pause = false;
static unsigned s_hold_override_ms = 0;
static bool s_deferred_load = false;
static bool s_reissuing = false;
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

static void arcade_flush(void)
{
	if (is_arcade() && spi_uio_cmd(UIO_CHK_UPLOAD)) arcade_nvm_save();
}

static void enter_hold(void)
{
	paint_banner("Saving...");
	raise_signal();
	arcade_flush();

	s_owner = ZOSD_SAVE;
	s_writes_hold = 0;
	if (s_hold_override_ms)
	{
		s_min_deadline = GetTimer(s_hold_override_ms);
		s_quiet_deadline = s_min_deadline;
		s_hold_cap = s_min_deadline;
	}
	else
	{
		s_min_deadline = GetTimer(ZSAVE_MIN_HOLD_MS);
		s_quiet_deadline = GetTimer(ZSAVE_QUIET_MS);
		s_hold_cap = GetTimer(ZSAVE_MAX_HOLD_MS);
	}
	s_state = ZSAVE_HOLD;
}

// Re-issue a core load that was held back for a flush. Runs from zaparoo_poll
// on a clean stack, so it cannot recurse into user_io_poll the way an inline
// wait inside fpga_load_rbf would.
static void reissue_load(void)
{
	s_deferred_load = false;
	s_owner = ZOSD_NONE;
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
	if (s_deferred_load)
	{
		// No point reporting "Saved" a frame before the FPGA is reconfigured.
		if (release) lower_signal();
		reissue_load();
		return;
	}

	if (s_resume_pause)
	{
		// Leave the signal up and hand ownership back to the pause.
		paint_banner("Paused");
		s_owner = ZOSD_PAUSE;
	}
	else
	{
		if (release) lower_signal();
		s_owner = ZOSD_NONE;
		if (release) Info("Saved", ZSAVE_REPORT_MS);
	}

	s_writes_after = 0;
	s_report_deadline = GetTimer(ZSAVE_REPORT_MS);
	s_state = ZSAVE_REPORT;
}

// True when this core has battery backing: slot 0 is where FileGenerateSavePath
// mounts the .sav, and that mount is the only thing that sets use_save.
static bool has_battery_save(void)
{
	fileTYPE *f = get_image(0);
	return f && f->path[0] && strcasestr(f->path, "saves/");
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

	bool battery = has_battery_save();
	bool arcade_pending = is_arcade() && spi_uio_cmd(UIO_CHK_UPLOAD);

	if (!battery)
	{
		// Arcade NVRAM needs no hold, so do it here and let the load proceed.
		if (arcade_pending) arcade_nvm_save();
		return false;
	}

	snprintf(s_rbf, sizeof(s_rbf), "%s", rbf);
	snprintf(s_cfg, sizeof(s_cfg), "%s", cfg ? cfg : "");
	snprintf(s_xml, sizeof(s_xml), "%s", xml ? xml : "");
	s_deferred_load = true;

	// A save already running just adopts the new target when it reports.
	if (s_state == ZSAVE_IDLE)
	{
		s_hold_override_ms = 0;
		s_writes_hold = 0;
		s_writes_after = 0;
		s_resume_pause = false;
		s_state = ZSAVE_ARMED;
	}

	printf("zaparoo_save: holding core load for a flush\n");
	return true;
}

bool zaparoo_save_request(unsigned hold_ms)
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
		arcade_flush();
		printf("zaparoo_save: OSD already open, arcade NVRAM only\n");
		return true;
	}

	s_hold_override_ms = hold_ms;
	s_writes_hold = 0;
	s_writes_after = 0;
	s_resume_pause = (s_owner == ZOSD_PAUSE);
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

bool zaparoo_pause_active(void)
{
	return s_owner == ZOSD_PAUSE;
}

void zaparoo_pause_set(bool on)
{
	if (on == zaparoo_pause_active()) return;

	if (on)
	{
		if (is_menu())
		{
			printf("zaparoo_pause: no core loaded\n");
			return;
		}
		if (menu_present())
		{
			printf("zaparoo_pause: OSD already open, core already paused\n");
			return;
		}
		if (s_state != ZSAVE_IDLE)
		{
			// A save is in flight; it hands over to the pause when it reports.
			s_resume_pause = true;
			return;
		}
		paint_banner("Paused");
		raise_signal();
		s_owner = ZOSD_PAUSE;
		printf("zaparoo_pause: on\n");
	}
	else
	{
		s_resume_pause = false;
		if (s_state != ZSAVE_IDLE) return;
		if (s_owner == ZOSD_PAUSE)
		{
			lower_signal();
			s_owner = ZOSD_NONE;
		}
		printf("zaparoo_pause: off\n");
	}
}

void zaparoo_poll(void)
{
	// The real OSD always wins. If it took over, or something else lowered the
	// signal, drop ownership rather than fight: re-asserting would strobe the
	// core against HandleUI.
	if (s_state == ZSAVE_IDLE)
	{
		if (s_owner == ZOSD_PAUSE && (menu_present() || !user_io_osd_is_visible()))
		{
			printf("zaparoo_pause: signal taken over, releasing\n");
			s_owner = ZOSD_NONE;
		}
		return;
	}

	switch (s_state)
	{
	case ZSAVE_ARMED:
		if (s_resume_pause)
		{
			lower_signal();
			s_dip_deadline = GetTimer(ZSAVE_DIP_MS);
			s_state = ZSAVE_DIP;
			break;
		}
		enter_hold();
		break;

	case ZSAVE_DIP:
		if (CheckTimer(s_dip_deadline)) enter_hold();
		break;

	case ZSAVE_HOLD:
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
			// The user opened the real OSD over us; it owns the signal now, so
			// do not lower it. A held-back core load still has to happen.
			printf("zaparoo_save: OSD opened during save, releasing\n");
			if (s_deferred_load)
			{
				reissue_load();
				break;
			}
			s_owner = ZOSD_NONE;
			s_state = ZSAVE_IDLE;
			break;
		}
		if (CheckTimer(s_hold_cap))
		{
			printf("zaparoo_save: hold cap reached (writes=%d)\n", s_writes_hold);
			enter_report(true);
			break;
		}
		if (CheckTimer(s_min_deadline) && CheckTimer(s_quiet_deadline)) enter_report(true);
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
