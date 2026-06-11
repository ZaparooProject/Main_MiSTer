// Periodically pulses a core's "Save Backup RAM" / "Save Memory Card"
// status bit so the core flushes dirty cartridge SRAM to its mounted save
// file without the user having to open the OSD. The pulse is byte-for-byte
// what the OSD menu item does (menu.cpp does user_io_status_set(opt, 1)
// then (opt, 0) back to back), so no new write path to the SD card exists.
//
// Mechanism credit: Biduleman's SNES_MiSTer_DirectSave fork (branch
// skip-osd-save) solves the same problem core-side by removing the
// OSD_STATUS gate from bk_save. We trigger the manual-save status bit
// instead so cores stay unmodified, and replicate the OSD's own safety
// gates HPS-side (see the guard chain in auto_save_poll).
//
// Design doc with per-core verification: AUTO_SAVE_PLAN.md (repo root).

#include "auto_save.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../cfg.h"
#include "../../file_io.h"
#include "../../hardware.h"
#include "../../spi.h"
#include "../../user_io.h"

// Settle window after a save-image mount before the first pulse can fire —
// gives the core time to read the existing save back into SRAM first.
static const unsigned long SETTLE_MS = 5000;

// Quiet window after save-image sector writes. A flush in progress means we
// must not snapshot (torn copy) or re-trigger (the core may ignore or
// restart the flush).
static const unsigned long QUIET_MS = 1000;

// Rolling backup generations: <save>.bak.0 (newest) .. .bak.N-1 (oldest).
static const int BAK_GENERATIONS = 3;

// Absolute path of a mounted save image plus room for ".bak.N.tmp".
#define AS_PATH_LEN  1224
#define AS_NAME_LEN  (AS_PATH_LEN + 16)

// One entry per SD slot (sd_image[16] in user_io.cpp). N64 mounts several
// save files (cart save + controller paks) at different indexes, so a
// single path is not enough.
#define AS_MAX_SLOTS 16

struct as_slot_t
{
	bool mounted;
	bool denied;          // non-pulse-safe save type (.fla) in this slot
	bool dirty_by_core;   // save-image sector writes seen since last baseline
	bool baseline_valid;  // mtime/size below are meaningful
	time_t mtime;         // external-modification baseline (Layer 4)
	off_t size;
	char path[AS_PATH_LEN];
};

static as_slot_t s_slots[AS_MAX_SLOTS];

static char s_core_name[64] = {};
static bool s_scanned_for_core = false;

// Matched save-trigger menu item: status bit (as "[N]" for
// user_io_status_set) plus the H/D hide/disable prefixes of that conf line.
// Uppercase H/D block the item while the menumask bit is SET, lowercase
// h/d while it is CLEAR — same semantics as menu.cpp's option parser.
static char s_bit_opt[16] = {};
static bool s_have_trigger = false;
static uint32_t s_hd_set_mask = 0;
static uint32_t s_hd_clr_mask = 0;

// Whole-core disarm: external modification detected (Layer 4), cleared by
// the next mount. The .fla deny lives per-slot so it can't outlive its
// mount (N64 remounts saves slot by slot on every ROM load).
static bool s_disarmed = false;

static unsigned long s_settle_timer = 0;
static unsigned long s_quiet_timer = 0;
static unsigned long s_next_pulse_timer = 0;

// One-shot log latches for guard conditions that recur every interval.
static bool s_logged_hdmask = false;

static int hexchar_to_bit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'V') return c - 'A' + 10;
	return -1;
}

// Walk the option prefixes the same way menu.cpp does: a run of
// H<x>/h<x>/D<x>/d<x> hide/disable tags, then an optional P<digit> page
// tag. Collects which menumask bits make the item unselectable.
static const char *skip_option_prefixes(const char *p, uint32_t *set_mask, uint32_t *clr_mask)
{
	while ((p[0] == 'H' || p[0] == 'h' || p[0] == 'D' || p[0] == 'd') && p[1])
	{
		int bit = hexchar_to_bit(p[1]);
		if (bit < 0) break;
		if (p[0] == 'H' || p[0] == 'D') *set_mask |= 1u << bit;
		else *clr_mask |= 1u << bit;
		p += 2;
	}
	if (p[0] == 'P' && p[1] >= '0' && p[1] <= '9' && p[2] != ',') p += 2;
	return p;
}

// Walk the bit identifier starting at *p (single hex char or "[N]" / "[N:M]").
// Returns pointer to the first char after the identifier, or NULL if invalid.
static const char *skip_bit_identifier(const char *p)
{
	if (*p == '[')
	{
		const char *q = strchr(p, ']');
		return q ? q + 1 : NULL;
	}
	if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'V') || (*p >= 'a' && *p <= 'v'))
		return p + 1;
	return NULL;
}

// Parse a save-trigger entry from a single conf-string line. Returns the
// status bit index (0..127) or -1 if this line doesn't match. Delegates the
// bit-number extraction to the upstream parser so we automatically track
// any identifier syntax it understands.
static int parse_save_trigger(const char *line, uint32_t *set_mask, uint32_t *clr_mask)
{
	uint32_t sm = 0, cm = 0;
	const char *p = skip_option_prefixes(line, &sm, &cm);
	if (*p != 'R' && *p != 'r' && *p != 'T' && *p != 't') return -1;
	int ex = (*p == 'r' || *p == 't') ? 1 : 0;
	const char *opt = p + 1;
	const char *after = skip_bit_identifier(opt);
	if (!after || *after != ',') return -1;

	const char *label = after + 1;
	static const char *targets[] = {
		"Save Backup RAM",
		"Save Memory Card",  // also matches PSX's "Save Memory Cards"
		NULL
	};
	bool match = false;
	for (int i = 0; targets[i]; i++)
	{
		if (strstr(label, targets[i])) { match = true; break; }
	}
	if (!match) return -1;

	int start = 0;
	if (!user_io_status_bits(opt, &start, NULL, ex, 1)) return -1;
	*set_mask = sm;
	*clr_mask = cm;
	return start;
}

static void scan_confstr_for_trigger(void)
{
	s_have_trigger = false;
	s_bit_opt[0] = '\0';
	s_hd_set_mask = 0;
	s_hd_clr_mask = 0;

	for (int i = 0; ; i++)
	{
		char *line = user_io_get_confstr(i);
		if (!line) break;
		int bit = parse_save_trigger(line, &s_hd_set_mask, &s_hd_clr_mask);
		if (bit >= 0)
		{
			snprintf(s_bit_opt, sizeof(s_bit_opt), "[%d]", bit);
			s_have_trigger = true;
			printf("auto_save: core '%s' trigger bit=%d hd_set=%08X hd_clr=%08X\n",
			       user_io_get_core_name(), bit, s_hd_set_mask, s_hd_clr_mask);
			return;
		}
	}
	printf("auto_save: no save-trigger menu item found for core '%s'\n",
	       user_io_get_core_name());
}

static bool fsync_parent_dir(const char *path)
{
	char dir[AS_NAME_LEN];
	strncpy(dir, path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char *slash = strrchr(dir, '/');
	if (!slash || slash == dir) return true;
	*slash = '\0';

	int fd = open(dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0) return false;
	bool ok = (fsync(fd) == 0);
	close(fd);
	return ok;
}

// Atomic copy: write to <dst>.tmp, fsync, rename over <dst>, fsync the
// directory. rename is not atomic on exFAT/FAT32, but the .tmp is itself a
// complete copy, so a power cut mid-rename still leaves a recoverable file.
static bool copy_file_atomic(const char *src, const char *dst)
{
	char tmp[AS_NAME_LEN + 8];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", dst) >= (int)sizeof(tmp)) return false;

	int fd_in = open(src, O_RDONLY);
	if (fd_in < 0)
	{
		printf("auto_save: open(%s) failed: %s\n", src, strerror(errno));
		return false;
	}

	int fd_out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_out < 0)
	{
		printf("auto_save: open(%s) failed: %s\n", tmp, strerror(errno));
		close(fd_in);
		return false;
	}

	char buf[8192];
	bool ok = true;
	for (;;)
	{
		ssize_t n = read(fd_in, buf, sizeof(buf));
		if (n == 0) break;
		if (n < 0) { ok = false; break; }
		ssize_t off = 0;
		while (off < n)
		{
			ssize_t w = write(fd_out, buf + off, (size_t)(n - off));
			if (w <= 0) { ok = false; break; }
			off += w;
		}
		if (!ok) break;
	}

	if (ok) ok = (fsync(fd_out) == 0);
	close(fd_out);
	close(fd_in);

	if (!ok)
	{
		unlink(tmp);
		return false;
	}
	if (rename(tmp, dst) != 0)
	{
		printf("auto_save: rename(%s) failed: %s\n", dst, strerror(errno));
		unlink(tmp);
		return false;
	}
	fsync_parent_dir(dst);
	return true;
}

// Layer 1: rolling generations. Drop the oldest, shift the rest, snapshot
// the current save into .bak.0.
static bool rotate_backups(const char *path)
{
	char from[AS_NAME_LEN], to[AS_NAME_LEN];

	snprintf(to, sizeof(to), "%s.bak.%d", path, BAK_GENERATIONS - 1);
	unlink(to);
	for (int i = BAK_GENERATIONS - 2; i >= 0; i--)
	{
		snprintf(from, sizeof(from), "%s.bak.%d", path, i);
		snprintf(to, sizeof(to), "%s.bak.%d", path, i + 1);
		rename(from, to);  // ENOENT is fine: generation not populated yet
	}

	snprintf(to, sizeof(to), "%s.bak.0", path);
	return copy_file_atomic(path, to);
}

// Layer 3: refuse to snapshot or pulse when the current save carries a
// corruption signature — better to keep the existing chain intact.
static bool sanity_check_ok(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) return false;

	char bak0[AS_NAME_LEN];
	snprintf(bak0, sizeof(bak0), "%s.bak.0", path);
	struct stat sb;
	if (stat(bak0, &sb) == 0 && sb.st_size > 1024 && st.st_size < sb.st_size / 2)
	{
		printf("auto_save: %s shrank %lld -> %lld bytes, skipping cycle\n",
		       path, (long long)sb.st_size, (long long)st.st_size);
		return false;
	}

	if (st.st_size == 0) return true;  // nothing to inspect; shrink rule above covers it

	int fd = open(path, O_RDONLY);
	if (fd < 0) return false;

	bool all_zero = true, all_ff = true;
	char buf[8192];
	for (;;)
	{
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n == 0) break;
		if (n < 0) { close(fd); return false; }
		for (ssize_t i = 0; i < n; i++)
		{
			if (buf[i] != 0x00) all_zero = false;
			if (buf[i] != (char)0xFF) all_ff = false;
		}
		if (!all_zero && !all_ff) break;
	}
	close(fd);

	if (all_zero || all_ff)
	{
		printf("auto_save: %s is all-%s, skipping cycle\n", path, all_zero ? "0x00" : "0xFF");
		return false;
	}
	return true;
}

static void reset_mount_state(void)
{
	memset(s_slots, 0, sizeof(s_slots));
	s_disarmed = false;
	s_settle_timer = 0;
	s_quiet_timer = 0;
	s_next_pulse_timer = 0;
	s_logged_hdmask = false;
}

static void detect_core_change(void)
{
	const char *current = user_io_get_core_name();
	if (!current) current = "";
	if (strcmp(current, s_core_name) != 0)
	{
		strncpy(s_core_name, current, sizeof(s_core_name) - 1);
		s_core_name[sizeof(s_core_name) - 1] = '\0';
		s_have_trigger = false;
		s_scanned_for_core = false;
		reset_mount_state();
	}
}

static bool any_slot_mounted(void)
{
	for (int i = 0; i < AS_MAX_SLOTS; i++)
	{
		if (s_slots[i].mounted) return true;
	}
	return false;
}

static bool any_slot_denied(void)
{
	for (int i = 0; i < AS_MAX_SLOTS; i++)
	{
		if (s_slots[i].mounted && s_slots[i].denied) return true;
	}
	return false;
}

void auto_save_on_save_mounted(unsigned char index, const char *path)
{
	if (cfg.auto_save == 0) return;  // disabled: leave no anchor/baseline files
	if (index >= AS_MAX_SLOTS || !path || !*path) return;

	// Sync core-change detection first so the poll's reset can't wipe a
	// mount that arrives in the same loop iteration as the core switch.
	detect_core_change();

	// Resolve relative-to-storage-root mount names to absolute paths; our
	// file I/O below must not depend on the process working directory.
	const char *full = getFullPath(path);
	as_slot_t *slot = &s_slots[index];
	if (strlen(full) >= sizeof(slot->path))
	{
		printf("auto_save: path too long, ignoring slot %d\n", index);
		return;
	}

	memset(slot, 0, sizeof(*slot));
	strcpy(slot->path, full);
	slot->mounted = true;

	// A new mount is a new generation: re-arm after an external-modification
	// disarm and reset the recurring-log latches.
	s_disarmed = false;
	s_logged_hdmask = false;

	size_t len = strlen(slot->path);
	if (len > 4 && !strcasecmp(slot->path + len - 4, ".fla"))
	{
		// N64 FlashRAM uses erase/program cycles; a pulse mid-cycle can
		// corrupt a sector. One trigger flushes every mounted save file,
		// so a mounted .fla holds off the whole core until it unmounts.
		slot->denied = true;
		printf("auto_save: %s is FlashRAM (.fla), auto-save disabled for this game\n", slot->path);
		return;
	}

	struct stat st;
	if (stat(slot->path, &st) == 0)
	{
		// Layer 2: per-mount anchor — the save as it was when this game
		// was loaded. Never overwritten until the next mount.
		char anchor[AS_NAME_LEN];
		snprintf(anchor, sizeof(anchor), "%s.mount", slot->path);
		if (!copy_file_atomic(slot->path, anchor))
		{
			printf("auto_save: anchor snapshot of %s failed\n", slot->path);
		}

		slot->baseline_valid = true;
		slot->mtime = st.st_mtime;
		slot->size = st.st_size;
	}
	// else: fresh game, save will be created on first write. No anchor.

	s_settle_timer = GetTimer(SETTLE_MS);
	if (cfg.auto_save) s_next_pulse_timer = GetTimer(cfg.auto_save * 1000UL);
}

void auto_save_on_save_unmounted(unsigned char index)
{
	if (index >= AS_MAX_SLOTS) return;
	if (!s_slots[index].mounted) return;
	memset(&s_slots[index], 0, sizeof(s_slots[index]));
	if (!any_slot_mounted()) reset_mount_state();
}

void auto_save_on_sector_write(int disk)
{
	if (cfg.auto_save == 0) return;
	if (disk < 0 || disk >= AS_MAX_SLOTS) return;
	if (!s_slots[disk].mounted) return;
	s_quiet_timer = GetTimer(QUIET_MS);
	s_slots[disk].dirty_by_core = true;
}

void auto_save_poll(void)
{
	if (cfg.auto_save == 0) return;

	detect_core_change();

	if (is_menu()) return;

	if (!s_scanned_for_core)
	{
		scan_confstr_for_trigger();
		s_scanned_for_core = true;
	}
	if (!s_have_trigger) return;
	if (!any_slot_mounted()) return;
	if (s_disarmed || any_slot_denied()) return;
	if (user_io_osd_is_visible()) return;

	if (s_settle_timer && !CheckTimer(s_settle_timer)) return;
	if (s_quiet_timer && !CheckTimer(s_quiet_timer)) return;

	if (!s_next_pulse_timer)
	{
		s_next_pulse_timer = GetTimer(cfg.auto_save * 1000UL);
		return;
	}
	if (!CheckTimer(s_next_pulse_timer)) return;

	// From here on, every outcome re-arms the interval timer.
	s_next_pulse_timer = GetTimer(cfg.auto_save * 1000UL);

	// The OSD greys out / hides the save item via the core's menumask
	// (e.g. SNES "D0RD" disables it while bk_ena is low — no battery RAM,
	// or GBA before the game first touches backup memory). A user can't
	// select a masked item, so neither may we.
	if (s_hd_set_mask || s_hd_clr_mask)
	{
		uint32_t hdmask = spi_uio_cmd16(UIO_GET_OSDMASK, 0);
		if ((hdmask & s_hd_set_mask) || (~hdmask & s_hd_clr_mask))
		{
			if (!s_logged_hdmask)
			{
				printf("auto_save: save item masked by core (mask=%04X), holding off\n", hdmask);
				s_logged_hdmask = true;
			}
			return;
		}
		s_logged_hdmask = false;
	}

	// Layer 4: external-modification guard. If a save changed on disk and
	// the core didn't write it (FTP import, SD swap), don't stomp it —
	// disarm until the next mount.
	for (int i = 0; i < AS_MAX_SLOTS; i++)
	{
		as_slot_t *slot = &s_slots[i];
		if (!slot->mounted) continue;

		struct stat st;
		if (stat(slot->path, &st) != 0) continue;  // not created yet

		bool changed = !slot->baseline_valid
		            || st.st_mtime != slot->mtime
		            || st.st_size != slot->size;
		if (changed && !slot->dirty_by_core)
		{
			printf("auto_save: %s changed externally, disarming until next load\n", slot->path);
			s_disarmed = true;
			return;
		}
		slot->baseline_valid = true;
		slot->mtime = st.st_mtime;
		slot->size = st.st_size;
		slot->dirty_by_core = false;
	}

	// Layer 3: validate every save before touching any backup chain.
	for (int i = 0; i < AS_MAX_SLOTS; i++)
	{
		as_slot_t *slot = &s_slots[i];
		if (!slot->mounted) continue;
		struct stat st;
		if (stat(slot->path, &st) != 0) continue;
		if (!sanity_check_ok(slot->path)) return;
	}

	// Layer 1: rotate the rolling generations and snapshot the current
	// state, so the pre-pulse save always survives at least one bad cycle.
	for (int i = 0; i < AS_MAX_SLOTS; i++)
	{
		as_slot_t *slot = &s_slots[i];
		if (!slot->mounted) continue;
		struct stat st;
		if (stat(slot->path, &st) != 0) continue;
		if (!rotate_backups(slot->path))
		{
			printf("auto_save: backup rotation of %s failed, skipping pulse\n", slot->path);
			return;
		}
	}

	// All guards passed — pulse the trigger exactly as the OSD menu does.
	user_io_status_set(s_bit_opt, 1);
	user_io_status_set(s_bit_opt, 0);
	printf("auto_save: pulsed %s for core '%s'\n", s_bit_opt, s_core_name);
}
