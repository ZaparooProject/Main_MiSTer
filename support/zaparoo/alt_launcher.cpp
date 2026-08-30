#include "alt_launcher.h"
#include "crt_settings.h"
#include "launcher_input_metadata.h"
#include "settings.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include "cfg.h"
#include "file_io.h"
#include "fpga_io.h"
#include "hardware.h"
#include "input.h"
#include "menu.h"
#include "scheduler.h"
#include "shmem.h"
#include "user_io.h"
#include "video.h"

// video_reinit() is intentionally internal to upstream's video module. The
// launcher uses it only for a bounded startup retry after video_init().
extern void video_reinit();

static const char s_launcher_path[] = "zaparoo/frontend";

// Timestamped trace line (ms since boot, same clock as GetTimer).
static void zlog(const char *fmt, ...)
{
	printf("alt_launcher t=%lu: ", GetTimer(0));
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

void alt_launcher_cfg_apply(void)
{
	// Override any user ini values: this fork is single-purpose. recents and
	// log_file_entry back Zaparoo's own integration points (/tmp/STARTPATH,
	// /tmp/OSD_VISIBLE, the gameid log).
	cfg.recents = 1;
	cfg.log_file_entry = 1;
	cfg.fb_terminal = 1;
}

static bool s_escaped = false;
static int s_installed_cache = -1;
static char s_installed_root[64] = {};

bool alt_launcher_installed(void)
{
	// Keyed on the storage root: FindStorage() runs a cfg_parse() with the root
	// forced to SD while waiting for USB, and this must not stay pinned to it.
	const char *root = getRootDir();
	if (s_installed_cache < 0 || strncmp(s_installed_root, root, sizeof(s_installed_root) - 1))
	{
		snprintf(s_installed_root, sizeof(s_installed_root), "%s", root);
		s_installed_cache = FileExists(s_launcher_path, 0) ? 1 : 0;
	}
	return s_installed_cache != 0;
}

void alt_launcher_installed_refresh(void)
{
	s_installed_cache = -1;
}

bool alt_launcher_enabled(void)
{
	return zaparoo_settings_frontend_enabled();
}

bool alt_launcher_configured(void)
{
	// After a clean exit / give-up, masquerade as not-configured so the rest
	// of the OSD reverts to stock menu behavior for the rest of this session.
	// Reboot re-execs MiSTer and resets this back to the file-existence cache.
	if (s_escaped) return false;
	return alt_launcher_installed() && alt_launcher_enabled();
}

uint16_t alt_launcher_fb_terminal_key(uint32_t mask, bool osd_button)
{
	if (!alt_launcher_configured())
		return 0;

	if (osd_button)
		return KEY_MENU;

	switch (mask)
	{
	case JOY_L2:
		return KEY_F1;
	case JOY_R2:
		return KEY_BACKSPACE;
	}

	return 0;
}

// Standard keyboards report letters + ESC; gamepads/mice report BTN_* instead.
static bool fd_is_keyboard(int fd)
{
	if (fd < 0) return false;
	unsigned char kb[(KEY_MAX + 7) / 8] = {};
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kb)), kb) < 0) return false;
	#define BIT_SET(b) (kb[(b) / 8] & (1 << ((b) % 8)))
	return BIT_SET(KEY_ESC) && BIT_SET(KEY_A) && BIT_SET(KEY_Z);
	#undef BIT_SET
}

int alt_launcher_kbd_grab(int fd)
{
	return (alt_launcher_active() && fd_is_keyboard(fd)) ? 1 : 0;
}

bool alt_launcher_kbd_to_frontend(uint16_t code)
{
	return alt_launcher_active() && !user_io_osd_is_visible()
		&& code < 256 && code != KEY_MENU && code != KEY_F12;
}

static pid_t s_pid = 0;
static unsigned long s_spawn_time = 0;
static bool s_pending_logged = false;
static int s_crash_count = 0;
static unsigned long s_respawn_timer = 0;
static unsigned long s_native_fb_mode_timer = 0;
static unsigned long s_hdmi_fb_reassert_timer = 0;
static int s_hdmi_fb_reasserts_remaining = 0;
static unsigned long s_hdmi_edid_retry_timer = 0;
static unsigned long s_tty_deadline = 0;
static unsigned long s_native_crt_finish_timer = 0;
static bool s_gave_up = false;
static bool s_init_pending = false;
static bool s_native_crt = false;
static uint8_t s_native_crt_mode = 0;
static bool s_resume_after_script = false;
static bool s_script_resume_crt = false;
static bool s_console_lease = false;
static bool s_console_state_published = false;
static char s_console_lease_nonce[65] = {};
static const int s_vt = 7;
static const char s_tty[] = "tty7";
static const char s_tty_path[] = "/dev/tty7";
static const char s_console_state_path[] = "/tmp/zaparoo_console_state";
static const char s_console_state_tmp_path[] = "/tmp/zaparoo_console_state.tmp";
static const char s_fb_mode_path[] = "/sys/module/MiSTer_fb/parameters/mode";
static const char s_crt_state_file[] = "zaparoo_launcher_crt.bin";
static char s_saved_fb_mode[64];
static bool s_saved_fb_mode_valid = false;

static void publish_console_state(const char *state, const char *nonce);

// Frontend exit code requesting a respawn after it rewrote
// zaparoo_launcher_crt.bin itself (launcher-owned CRT toggle).
#define ALT_LAUNCHER_EXIT_RELOAD 42

// A child alive this long is treated as stable: its exit does not count
// toward the crash-loop give-up limit.
#define ALT_LAUNCHER_STABLE_MS 10000

static unsigned long s_fb_watchdog_timer = 0;

// zaparoo_launcher_crt.bin layout (written by the frontend, and by the
// OSD toggle below):
//   byte 0: CRT enabled (0/1)
//   byte 1: video standard as the DDR word1 mode id - 0 NTSC 352x240,
//           1 480i 720x480, 2 PAL 352x288. Absent in legacy 1-byte
//           files; FileLoad partial-reads into the zeroed buffer, so
//           legacy reads as NTSC.
// The mode byte lives here (not only in the frontend's toml) because
// this side programs the framebuffer geometry before the spawn and
// re-asserts it ~1 s after - both writes must match the standard the
// frontend will render, or the re-assert stomps a PAL framebuffer back
// to 352x240 under a running Qt.
static void load_persisted_native_crt_state(bool *enabled, uint8_t *mode)
{
	uint8_t v[2] = {0, 0};
	FileLoadConfig(s_crt_state_file, v, sizeof(v));
	if (v[1] > 2) v[1] = 0;
	*enabled = v[0] != 0;
	*mode = v[1];
}

static bool load_persisted_native_crt(void)
{
	bool enabled;
	uint8_t mode;
	load_persisted_native_crt_state(&enabled, &mode);
	return enabled;
}

static void set_launcher_fb_mode(int fmt, int rb, int width, int height, int stride, bool log = true)
{
	FILE *fp = fopen(s_fb_mode_path, "wt");
	if (!fp)
	{
		printf("alt_launcher: unable to set fb mode: %s\n", strerror(errno));
		return;
	}

	fprintf(fp, "%d %d %d %d %d\n", fmt, rb, width, height, stride);
	fclose(fp);
	if (log)
		printf("alt_launcher: fb mode set to %dx%d fmt=%d stride=%d\n", width, height, fmt, stride);
}

static void save_current_fb_mode(void)
{
	if (s_saved_fb_mode_valid)
		return;

	FILE *fp = fopen(s_fb_mode_path, "rt");
	if (!fp)
	{
		printf("alt_launcher: unable to read fb mode: %s\n", strerror(errno));
		return;
	}

	if (fgets(s_saved_fb_mode, sizeof(s_saved_fb_mode), fp))
	{
		size_t len = strlen(s_saved_fb_mode);
		while (len && (s_saved_fb_mode[len - 1] == '\n' || s_saved_fb_mode[len - 1] == '\r'))
			s_saved_fb_mode[--len] = 0;
		s_saved_fb_mode_valid = len != 0;
		if (s_saved_fb_mode_valid)
			printf("alt_launcher: saved fb mode '%s'\n", s_saved_fb_mode);
	}
	fclose(fp);
}

static void restore_saved_fb_mode(void)
{
	if (!s_saved_fb_mode_valid)
	{
		set_launcher_fb_mode(8888, 1, 960, 720, 3840);
		return;
	}

	FILE *fp = fopen(s_fb_mode_path, "wt");
	if (!fp)
	{
		printf("alt_launcher: unable to restore fb mode: %s\n", strerror(errno));
		return;
	}

	fprintf(fp, "%s\n", s_saved_fb_mode);
	fclose(fp);
	printf("alt_launcher: restored fb mode '%s'\n", s_saved_fb_mode);
	s_saved_fb_mode_valid = false;
}

static void set_native_crt_fb_mode(bool log = true)
{
	switch (s_native_crt_mode)
	{
	case 1: // 480i60, rendered progressive by the frontend
		set_launcher_fb_mode(8888, 1, 720, 480, 2880, log);
		break;
	case 2: // PAL 50p
		set_launcher_fb_mode(8888, 1, 352, 288, 1408, log);
		break;
	default: // NTSC 60p
		set_launcher_fb_mode(8888, 1, 352, 240, 1408, log);
		break;
	}
}

static void blank_native_crt_fb(void)
{
	// CRT path doesn't read /dev/fb0 (kernel FB at 0x22000000). The frontend
	// runs a worker that copies the top-left 352x240 from /dev/fb0 into a
	// separate FPGA-mapped region at 0x3A000000 (two control words + two
	// framebuffers, 3 MB under the v2 contract). The core scans out from
	// that region. A zeroed word0 means "writer stopped", so this blank
	// deterministically parks the core on its noise pattern — and clears any
	// previous session's frame — until the frontend's writer publishes.
	const uint32_t native_addr = 0x3A000000u;
	const uint32_t native_size = 0x00300000u;
	void *p = shmem_map(native_addr, native_size);
	if (!p)
	{
		printf("alt_launcher: blank native shmem_map(0x%x, %u) failed\n", native_addr, native_size);
		return;
	}
	memset(p, 0, native_size);
	shmem_unmap(p, native_size);
	printf("alt_launcher: blanked %u bytes of CRT native video DDR at 0x%x\n", native_size, native_addr);
}

static void zero_native_crt_words(void)
{
	// Only a zeroed word0 releases the core from the last published frame
	// (there is no core-side disable bit), so teardown must clear the
	// control words even when the frontend crashed before its own stop
	// handler could.
	const uint32_t native_addr = 0x3A000000u;
	const uint32_t map_size = 0x1000u;
	void *p = shmem_map(native_addr, map_size);
	if (!p)
	{
		printf("alt_launcher: zero words shmem_map(0x%x, %u) failed\n", native_addr, map_size);
		return;
	}
	memset(p, 0, 8);
	shmem_unmap(p, map_size);
}

static void clear_launcher_tty(void)
{
	int tty_fd = open(s_tty_path, O_WRONLY | O_CLOEXEC);
	if (tty_fd >= 0)
	{
		static const char blank[] = "\033[?25l\033[40m\033[30m\033[2J\033[H";
		if (write(tty_fd, blank, sizeof(blank) - 1) < 0) {}
		close(tty_fd);
	}
}

static void reset_launcher_tty(void)
{
	int tty_fd = open(s_tty_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (tty_fd >= 0)
	{
		ioctl(tty_fd, KDSETMODE, KD_TEXT);
		ioctl(tty_fd, KDSKBMODE, K_XLATE);
		struct vt_mode vtmode;
		memset(&vtmode, 0, sizeof(vtmode));
		vtmode.mode = VT_AUTO;
		ioctl(tty_fd, VT_SETMODE, &vtmode);

		struct termios tio;
		if (!tcgetattr(tty_fd, &tio))
		{
			tio.c_iflag |= BRKINT | ICRNL | IXON | IMAXBEL;
			tio.c_iflag &= ~(IGNBRK | INLCR | IGNCR | IXOFF);
			tio.c_oflag |= OPOST | ONLCR;
			tio.c_lflag |= ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
			tio.c_lflag &= ~(NOFLSH | TOSTOP);
			tio.c_cflag |= CREAD;
			tio.c_cc[VMIN] = 1;
			tio.c_cc[VTIME] = 0;
			tcsetattr(tty_fd, TCSANOW, &tio);
		}

		static const char reset[] = "\033[0m\033[?25h\033[37m\033[40m\033[2J\033[H";
		if (write(tty_fd, reset, sizeof(reset) - 1) < 0) {}
		close(tty_fd);
	}
}

static bool launcher_tty_ready(pid_t pid)
{
	char fd_path[64];
	char target[128];
	for (int fd = 0; fd < 3; fd++)
	{
		snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/%d", pid, fd);
		ssize_t len = readlink(fd_path, target, sizeof(target) - 1);
		if (len > 0)
		{
			target[len] = 0;
			if (!strcmp(target, s_tty_path))
				return true;
		}
	}

	return false;
}

static void disable_native_crt_path(void)
{
	zero_native_crt_words();
	video_fb_enable(0);
	set_vga_fb(0);
	restore_saved_fb_mode();
	s_tty_deadline = 0;
	s_native_fb_mode_timer = 0;
	s_native_crt_finish_timer = 0;
}

static void prepare_native_crt_path(void)
{
	// Refresh the standard from the state file on every CRT spawn: the
	// frontend rewrites the mode byte (video standard change) before
	// exiting with ALT_LAUNCHER_EXIT_RELOAD, and the T+1s re-assert in
	// alt_launcher_poll must use the same geometry.
	{
		bool enabled;
		load_persisted_native_crt_state(&enabled, &s_native_crt_mode);
		(void)enabled;
	}

	set_vga_fb(0);
	video_fb_enable(0);
	save_current_fb_mode();
	set_native_crt_fb_mode(false);
	s_native_crt_finish_timer = GetTimer(1);
	if (!s_native_crt_finish_timer) s_native_crt_finish_timer = 1;
}

static void finish_native_crt_path(void)
{
	// v2 contract: no status[9]. Blank the DDR scan-out region, then re-assert
	// the fb geometry at T+1s so the frontend's vmode can't leave a non-NTSC
	// standard stomped back to 352x240.
	s_native_crt_finish_timer = 0;

	blank_native_crt_fb();

	s_native_fb_mode_timer = GetTimer(1000);
	if (!s_native_fb_mode_timer) s_native_fb_mode_timer = 1;
}

// escaped: the user quit the frontend, so the OSD reverts to stock for the
// session. A crash-loop give-up keeps the trimmed OSD (and its Frontend
// page) so CRT settings can still be fixed from there.
static void return_to_normal_mode(bool escaped)
{
	user_io_osd_key_enable(1);
	reset_launcher_tty();
	video_menu_bg(user_io_status_get("[3:1]"));
	if (s_native_crt) disable_native_crt_path();
	else video_fb_enable(0);
	s_native_crt = false;
	s_respawn_timer = 0;
	s_hdmi_fb_reassert_timer = 0;
	s_hdmi_fb_reasserts_remaining = 0;
	s_hdmi_edid_retry_timer = 0;
	s_crash_count = 0;
	s_gave_up = true;
	s_escaped = escaped;
}

static void reset_launcher_state(void)
{
	s_pid = 0;
	s_respawn_timer = 0;
	s_tty_deadline = 0;
	s_native_fb_mode_timer = 0;
	s_hdmi_fb_reassert_timer = 0;
	s_hdmi_fb_reasserts_remaining = 0;
	s_hdmi_edid_retry_timer = 0;
	s_native_crt_finish_timer = 0;
	s_fb_watchdog_timer = 0;
	s_crash_count = 0;
	s_gave_up = false;
	s_init_pending = false;
	s_escaped = false;
	s_resume_after_script = false;
	s_script_resume_crt = false;
}

static void kill_launcher(pid_t pid, int sig)
{
	if (kill(-pid, sig) && errno == ESRCH)
		kill(pid, sig);
}

static void wait_launcher_stopped(pid_t pid)
{
	kill_launcher(pid, SIGTERM);
	for (int i = 0; i < 50; i++)
	{
		if (waitpid(pid, NULL, WNOHANG) == pid)
		{
			s_pid = 0;
			break;
		}
		usleep(10000);
	}
	if (s_pid)
	{
		kill_launcher(pid, SIGKILL);
		// Bounded — don't wedge the UI if the task is stuck in D-state.
		for (int i = 0; i < 100; i++)
		{
			if (waitpid(pid, NULL, WNOHANG) == pid)
			{
				s_pid = 0;
				break;
			}
			usleep(10000);
		}
	}
}

// Kill frontend processes this Main did not spawn: a previous Main that was
// killed (deploy) or crashed leaves its child holding fb0 and the VT. Matches
// any exe under the launcher path so a binary renamed underneath a running
// process (deploy rotates it to .bak) is caught too. Bounded wait.
static void kill_stale_frontends(void)
{
	const char *prefix = getFullPath(s_launcher_path);
	size_t plen = strlen(prefix);
	pid_t self = getpid();
	pid_t stale[16];
	int n = 0;

	DIR *d = opendir("/proc");
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d)) && n < (int)(sizeof(stale) / sizeof(stale[0])))
	{
		if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
		pid_t pid = (pid_t)atoi(de->d_name);
		if (pid <= 1 || pid == self) continue;
		char link[64], target[256];
		snprintf(link, sizeof(link), "/proc/%d/exe", pid);
		ssize_t len = readlink(link, target, sizeof(target) - 1);
		if (len <= 0) continue;
		target[len] = 0;
		if (strncmp(target, prefix, plen)) continue;
		stale[n++] = pid;
		kill_launcher(pid, SIGKILL);
	}
	closedir(d);
	if (!n) return;

	zlog("killed %d stale frontend process(es), first pid=%d", n, stale[0]);
	for (int i = 0; i < 100; i++)
	{
		bool alive = false;
		for (int k = 0; k < n; k++)
		{
			if (!kill(stale[k], 0)) { alive = true; break; }
		}
		if (!alive) break;
		usleep(10000);
	}
}

static void release_launcher_video(void)
{
	s_hdmi_fb_reassert_timer = 0;
	s_hdmi_fb_reasserts_remaining = 0;
	if (s_native_crt)
	{
		s_native_crt = false;
		disable_native_crt_path();
	}
	else
	{
		video_fb_enable(0);
	}
}

static void exec_launcher_child(const char *path)
{
	setenv("LC_ALL", "en_US.UTF-8", 1);
	setenv("HOME", "/root", 1);

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);

	setsid();
	// Die with Main (a killed or crashed Main must not leave a frontend
	// holding fb0 and the VT for the next Main to fight with).
	prctl(PR_SET_PDEATHSIG, SIGKILL);

	int tty_fd = open(s_tty_path, O_RDWR);
	if (tty_fd >= 0)
	{
		ioctl(tty_fd, TIOCSCTTY, 0);
		dup2(tty_fd, STDIN_FILENO);
		dup2(tty_fd, STDOUT_FILENO);
		dup2(tty_fd, STDERR_FILENO);
		if (tty_fd > STDERR_FILENO)
			close(tty_fd);
	}

	static const char clear[] = "\033[0m\033[?25l\033[37m\033[40m\033[2J\033[H";
	if (write(STDOUT_FILENO, clear, sizeof(clear) - 1) < 0) {}

	if (s_native_crt)
		execl(path, path, "--crt", NULL);
	else
		execl(path, path, NULL);
	_exit(1);
}

// Bounded replacement for video_chvt(): its VT_WAITACTIVE blocks forever if the
// target process stalls bringing up video, which would wedge the poll cothread.
static bool switch_to_vt(int vt)
{
	int fd = open("/dev/tty0", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return false;

	if (ioctl(fd, VT_ACTIVATE, vt)) printf("alt_launcher: VT_ACTIVATE fails\n");

	// Yield to the scheduler rather than usleep() so the poll cothread stays
	// cooperative while we wait (bounded) for the VT to become active.
	bool active = false;
	unsigned long deadline = GetTimer(500);
	for (;;)
	{
		struct vt_stat st;
		if (!ioctl(fd, VT_GETSTATE, &st) && st.v_active == vt)
		{
			active = true;
			break;
		}
		if (CheckTimer(deadline)) break;
		scheduler_yield();
	}

	close(fd);
	return active;
}

static void finalize_spawn(bool tty_ready)
{
	// Defer the VT/fb takeover until the FPGA/HDMI has settled; s_tty_deadline
	// stays armed so this retries on a later alt_launcher_poll pass.
	if (!is_fpga_ready(1))
	{
		zlog("finalize deferred: fpga not ready");
		return;
	}

	s_tty_deadline = 0;
	bool vt_ok = switch_to_vt(s_vt);
	zlog("finalize: tty_ready=%d vt_active=%d fb_state=%d", tty_ready, vt_ok, video_fb_state());
	if (!s_native_crt)
	{
		// The frontend configures /dev/fb0 before Qt starts. Only publish its
		// existing buffer here; rewriting the mode after Qt has painted clears
		// the frame and leaves black until each region becomes dirty again.
		video_fb_reassert();
		// Returning to the menu can keep settling its video mode after the
		// frontend is already ready. Re-assert across that init window so a
		// late reset cannot leave a live framebuffer behind menu static.
		s_hdmi_fb_reasserts_remaining = 5;
		s_hdmi_fb_reassert_timer = GetTimer(1000);
		if (!s_hdmi_fb_reassert_timer) s_hdmi_fb_reassert_timer = 1;
	}
	else
		input_switch(0);

	// The frontend grabs input as soon as it starts. If the OSD is still
	// up (e.g. user toggled CRT mode or hit Reboot from System Settings),
	// it would trap input with no way to dismiss it — drop it now.
	if (menu_present()) MenuHide();
}

static void spawn(void)
{
	if (!s_console_state_published) publish_console_state("ready", NULL);

	char path[2100];
	strncpy(path, getFullPath(s_launcher_path), sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	user_io_osd_key_enable(0);
	clear_launcher_tty();

	zlog("spawn: native_crt=%d fb_state=%d edid=%d menu_present=%d", s_native_crt, video_fb_state(), video_get_edid(NULL, NULL), menu_present());
	if (s_native_crt)
	{
		prepare_native_crt_path();
		printf("alt_launcher: native CRT path prepared\n");
	}
	else
	{
		// Accept the child's pre-Qt vmode request deterministically. Enabling only
		// from finalize_spawn races the child: video_cmd ignores fb_cmd while the
		// HPS framebuffer is disabled. Re-assert without scheduling the launcher's
		// current geometry back into the kernel; that delayed write can otherwise
		// overwrite the child's smaller framebuffer after Qt has mapped it.
		video_fb_reassert();
		printf("alt_launcher: HPS framebuffer path enabled\n");
	}

	s_pid = fork();
	if (s_pid < 0)
	{
		printf("alt_launcher: fork failed: %s\n", strerror(errno));
		s_pid = 0;
		user_io_osd_key_enable(1);
		if (s_native_crt) disable_native_crt_path();
		else video_fb_enable(0);
		return;
	}
	if (!s_pid)
	{
		exec_launcher_child(path);
	}
	s_spawn_time = GetTimer(0);
	zlog("spawned pid=%d path=%s", s_pid, path);

	input_export_launcher_metadata();

	s_tty_deadline = GetTimer(1000);
	if (!s_tty_deadline) s_tty_deadline = 1;
}

bool alt_launcher_active(void)
{
	return s_pid != 0;
}

bool alt_launcher_owns_screen(void)
{
	return s_pid != 0 || s_init_pending || s_respawn_timer != 0;
}

bool alt_launcher_console_lease_active(void)
{
	return s_console_lease;
}

bool alt_launcher_scheduler_sleep_enabled(void)
{
	return s_pid || s_init_pending || s_respawn_timer || s_tty_deadline || s_native_crt_finish_timer || s_native_fb_mode_timer || s_hdmi_fb_reassert_timer || s_hdmi_edid_retry_timer;
}

bool alt_launcher_handle_video_fb_config(void)
{
	if (s_native_crt || (!s_pid && !s_init_pending && !s_respawn_timer)) return false;

	// Own HDMI fb configuration from queued initialization through the live
	// child. This prevents pre-spawn module writes from landing after the
	// frontend's vmode, and publishes its geometry against later output changes.
	video_fb_reassert();
	return true;
}

bool alt_launcher_native_crt(void)
{
	return s_native_crt && s_pid != 0;
}

void alt_launcher_init(bool native_crt)
{
	if (!alt_launcher_configured() || s_pid || s_gave_up)
		return;
	s_crash_count = 0;
	s_respawn_timer = 0;
	s_tty_deadline = 0;
	s_native_crt = native_crt;
	// user_io can request the launcher before video_init() has read EDID and
	// selected the HDMI mode. Defer the child until that setup returns. If the
	// first EDID read missed a slow monitor, allow one bounded retry after the
	// link has had another 500 ms to settle.
	s_hdmi_edid_retry_timer = native_crt ? 0 : GetTimer(500);
	if (!native_crt && !s_hdmi_edid_retry_timer) s_hdmi_edid_retry_timer = 1;
	s_init_pending = true;
	s_pending_logged = false;
	zlog("init queued: native_crt=%d edid=%d fb_state=%d", native_crt, video_get_edid(NULL, NULL), video_fb_state());
}

void alt_launcher_prepare_for_script(void)
{
	reset_launcher_tty();
	if (!s_pid)
		return;

	printf("alt_launcher: suspending launcher for script\n");
	s_resume_after_script = true;
	s_script_resume_crt = s_native_crt;
	pid_t pid = s_pid;
	wait_launcher_stopped(pid);
	user_io_osd_key_enable(1);
	s_respawn_timer = 0;
	s_tty_deadline = 0;
	s_crash_count = 0;
	s_init_pending = false;
	s_gave_up = false;
	release_launcher_video();
	reset_launcher_tty();
}

void alt_launcher_resume_after_script(void)
{
	if (!s_resume_after_script)
	{
		reset_launcher_tty();
		return;
	}

	bool crt = s_script_resume_crt;
	s_resume_after_script = false;
	s_script_resume_crt = false;
	s_gave_up = false;
	s_escaped = false;
	printf("alt_launcher: resuming launcher after script (crt=%d)\n", crt);
	alt_launcher_init(crt);
}

static void publish_console_state(const char *state, const char *nonce)
{
	FILE *fp = fopen(s_console_state_tmp_path, "wt");
	if (!fp)
	{
		printf("alt_launcher: unable to publish console state: %s\n", strerror(errno));
		return;
	}

	fprintf(fp, "1 %d %s %s\n", getpid(), state, nonce && *nonce ? nonce : "-");
	if (fclose(fp))
	{
		unlink(s_console_state_tmp_path);
		return;
	}
	if (rename(s_console_state_tmp_path, s_console_state_path))
	{
		printf("alt_launcher: unable to replace console state: %s\n", strerror(errno));
		unlink(s_console_state_tmp_path);
		return;
	}
	s_console_state_published = true;
}

static bool valid_console_nonce(const char *nonce)
{
	if (!nonce || !*nonce || strlen(nonce) >= sizeof(s_console_lease_nonce)) return false;
	for (const char *p = nonce; *p; p++)
	{
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-'))
			return false;
	}
	return true;
}

bool alt_launcher_command(const char *cmd)
{
	// installed(), not configured(): a release must never become unanswerable
	// if the frontend is disabled while a lease is held, or the OSD key stays
	// off with the framebuffer up for good.
	if (!alt_launcher_installed() || strncmp(cmd, "zaparoo_console ", 16)) return false;

	char action[16] = {};
	char nonce[65] = {};
	int vt = 0;
	int fields = sscanf(cmd + 16, "%15s %64s %d", action, nonce, &vt);
	if (fields < 2 || !valid_console_nonce(nonce))
	{
		printf("alt_launcher: invalid console command: %s\n", cmd);
		return true;
	}

	if (!strcmp(action, "acquire") && fields == 3 && vt >= 1 && vt <= 63)
	{
		// New leases still require an enabled frontend; releases do not.
		if (!alt_launcher_configured())
		{
			publish_console_state("failed", nonce);
			return true;
		}

		if (s_console_lease)
		{
			if (!strcmp(nonce, s_console_lease_nonce)) publish_console_state("acquired", nonce);
			else publish_console_state("busy", nonce);
			return true;
		}

		alt_launcher_prepare_for_script();
		if (!switch_to_vt(vt))
		{
			publish_console_state("failed", nonce);
			alt_launcher_resume_after_script();
			return true;
		}
		video_fb_enable(1);
		s_console_lease = true;
		if (menu_present()) MenuHide();
		user_io_osd_key_enable(0);
		strncpy(s_console_lease_nonce, nonce, sizeof(s_console_lease_nonce) - 1);
		publish_console_state("acquired", nonce);
		printf("alt_launcher: console lease acquired nonce=%s vt=%d\n", nonce, vt);
		return true;
	}

	if (!strcmp(action, "release") && fields == 2)
	{
		if (!s_console_lease || strcmp(nonce, s_console_lease_nonce))
		{
			publish_console_state("failed", nonce);
			return true;
		}

		video_fb_enable(0);
		s_console_lease = false;
		s_console_lease_nonce[0] = 0;
		alt_launcher_resume_after_script();
		publish_console_state("released", nonce);
		printf("alt_launcher: console lease released nonce=%s\n", nonce);
		return true;
	}

	printf("alt_launcher: unsupported console command: %s\n", cmd);
	return true;
}

void alt_launcher_poll(void)
{
	if (s_pid)
	{
		if (s_native_crt && s_native_crt_finish_timer && CheckTimer(s_native_crt_finish_timer))
		{
			finish_native_crt_path();
			printf("alt_launcher: native CRT path enabled\n");
		}

		if (s_native_crt && s_native_fb_mode_timer && CheckTimer(s_native_fb_mode_timer))
		{
			set_native_crt_fb_mode();
			s_native_fb_mode_timer = 0;
		}

		if (!s_native_crt && s_hdmi_fb_reassert_timer && CheckTimer(s_hdmi_fb_reassert_timer))
		{
			// Re-send scanout state without reconfiguring /dev/fb0 under Qt;
			// resetting the live kernel framebuffer clears it until dirty repaint.
			video_fb_reassert();
			if (--s_hdmi_fb_reasserts_remaining > 0)
			{
				s_hdmi_fb_reassert_timer = GetTimer(2000);
				if (!s_hdmi_fb_reassert_timer) s_hdmi_fb_reassert_timer = 1;
			}
			else
			{
				s_hdmi_fb_reassert_timer = 0;
			}
			zlog("HPS framebuffer path re-asserted (%d remaining)", s_hdmi_fb_reasserts_remaining);
		}

		int status;
		if (waitpid(s_pid, &status, WNOHANG) == s_pid)
		{
			s_pid = 0;
			s_tty_deadline = 0;
			s_hdmi_fb_reassert_timer = 0;
			s_hdmi_fb_reasserts_remaining = 0;
			user_io_osd_key_enable(1);
			bool exited = WIFEXITED(status);
			int exit_status = exited ? WEXITSTATUS(status) : 0;
			int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
			unsigned long uptime = GetTimer(0) - s_spawn_time;
			zlog("child exit: exited=%d code=%d sig=%d uptime=%lu ms crash_count=%d native_crt=%d",
			     exited, exit_status, sig, uptime, s_crash_count, s_native_crt);
			// The frontend rewrote zaparoo_launcher_crt.bin itself and wants
			// to be respawned under the new setting — release the current
			// video path and re-enter through the normal init machinery.
			if (exited && exit_status == ALT_LAUNCHER_EXIT_RELOAD)
			{
				bool crt = load_persisted_native_crt();
				printf("alt_launcher: reload requested, respawning (crt=%d)\n", crt);
				release_launcher_video();
				reset_launcher_tty();
				s_respawn_timer = 0;
				s_crash_count = 0;
				alt_launcher_init(crt);
				return;
			}
			// Only a clean exit is the user leaving the frontend. Signals
			// (including an external kill during a deploy) and non-zero codes
			// respawn. A child that ran long enough was not crash-looping, so
			// its exit restarts the strike count instead of adding to it.
			if (exited && exit_status == 0)
			{
				printf("alt_launcher: exited, returning to normal mode until restart\n");
				return_to_normal_mode(true);
				return;
			}
			if (uptime >= ALT_LAUNCHER_STABLE_MS) s_crash_count = 0;
			if (++s_crash_count >= 3)
			{
				printf("alt_launcher: giving up after 3 crashes\n");
				return_to_normal_mode(false);
				return;
			}
			s_respawn_timer = GetTimer(1000);
			if (!s_respawn_timer) s_respawn_timer = 1;
			return;
		}

		// Nothing legitimately turns the HPS framebuffer off under a live
		// HDMI frontend, but an HDMI hot-plug re-init (video_reinit ->
		// video_menu_bg) does exactly that. Re-assert so the frontend's
		// startup vmode probes and its later output never land on a
		// disabled framebuffer.
		if (!s_native_crt && !video_fb_state() && (!s_fb_watchdog_timer || CheckTimer(s_fb_watchdog_timer)))
		{
			s_fb_watchdog_timer = GetTimer(250);
			if (!s_fb_watchdog_timer) s_fb_watchdog_timer = 1;
			video_fb_reassert();
			zlog("watchdog: framebuffer was off, re-asserted (fb_state=%d)", video_fb_state());
		}

		if (s_tty_deadline && !s_native_crt_finish_timer)
		{
			bool tty_ready = launcher_tty_ready(s_pid);
			if (tty_ready || CheckTimer(s_tty_deadline))
				finalize_spawn(tty_ready);
		}
		return;
	}

	if (!alt_launcher_configured())
		return;

	if (s_init_pending)
	{
		if (!s_pending_logged)
		{
			s_pending_logged = true;
			zlog("first poll: edid=%d fb_state=%d fpga_ready=%d", video_get_edid(NULL, NULL), video_fb_state(), is_fpga_ready(1));
		}
		if (!s_native_crt && video_get_edid(NULL, NULL) == 0)
		{
			if (s_hdmi_edid_retry_timer && !CheckTimer(s_hdmi_edid_retry_timer)) return;
			unsigned long t0 = GetTimer(0);
			video_reinit();
			zlog("edid retry: video_reinit took %lu ms, edid=%d", GetTimer(0) - t0, video_get_edid(NULL, NULL));
		}
		s_hdmi_edid_retry_timer = 0;
		s_init_pending = false;
		spawn();
		return;
	}

	if (s_respawn_timer && CheckTimer(s_respawn_timer))
	{
		s_respawn_timer = 0;
		zlog("respawn timer fired");
		spawn();
	}
}

void alt_launcher_shutdown(void)
{
	fflush(stdout);
	if (!s_pid)
	{
		// installed(): orphans from a previous Main hold fb0/tty7 regardless
		// of the current enable setting.
		if (alt_launcher_installed()) kill_stale_frontends();
		reset_launcher_state();
		if (s_native_crt)
		{
			s_native_crt = false;
			disable_native_crt_path();
		}
		// No video_fb_enable(0) here. app_restart() calls this a second time,
		// after fpga_load_rbf has already put the game core in the FPGA, and
		// is_menu() is a per-process cache of orig_name, so it is still true:
		// video_fb_set()'s menu-background fallback would flip the disable
		// into FB_EN at menu geometry and write it into the new core.
		return;
	}

	pid_t pid = s_pid;
	kill_launcher(pid, SIGTERM);
	for (int i = 0; i < 50; i++)
	{
		if (waitpid(pid, NULL, WNOHANG) == pid)
		{
			s_pid = 0;
			break;
		}
		usleep(10000);
	}
	if (s_pid)
	{
		kill_launcher(pid, SIGKILL);
		// Bounded — don't wedge the UI if the task is stuck in D-state.
		for (int i = 0; i < 100; i++)
		{
			if (waitpid(pid, NULL, WNOHANG) == pid)
			{
				s_pid = 0;
				break;
			}
			usleep(10000);
		}
	}

	reset_launcher_state();
	if (s_native_crt)
	{
		s_native_crt = false;
		disable_native_crt_path();
	}
	else
	{
		video_fb_enable(0);
	}
}

bool alt_launcher_native_crt_persisted(void)
{
	return load_persisted_native_crt();
}

uint8_t alt_launcher_native_crt_mode(void)
{
	bool enabled;
	uint8_t mode;
	load_persisted_native_crt_state(&enabled, &mode);
	return mode;
}

// Stop a running launcher and drop every pending timer and flag.
// restore_bg repaints the menu wallpaper; a restart skips it because the
// incoming child owns the screen. alt_launcher_shutdown() is not usable here:
// it reaps the child itself, so neither the poll child-exit path nor
// return_to_normal_mode runs and user_io_osd_key_enable(1) never happens,
// which would leave the OSD unopenable.
static void stop_launcher(bool restore_bg)
{
	if (s_pid) wait_launcher_stopped(s_pid);
	user_io_osd_key_enable(1);
	release_launcher_video();
	reset_launcher_tty();
	// Menu core only: on a game core there is no menu background to restore.
	if (restore_bg && is_menu()) video_menu_bg(user_io_status_get("[3:1]"));
	s_respawn_timer = 0;
	s_tty_deadline = 0;
	s_init_pending = false;
	s_crash_count = 0;
	// Clear an earlier give-up or escape: an explicit action leaves a clean
	// slate either way, so a later enable is not blocked by a stale flag.
	s_gave_up = false;
	s_escaped = false;
	s_resume_after_script = false;
	s_script_resume_crt = false;
}

// Stop a running launcher and queue a fresh start under `crt`: the same
// re-entry steps as the ALT_LAUNCHER_EXIT_RELOAD path in alt_launcher_poll.
static void restart_launcher(bool crt)
{
	stop_launcher(false);
	alt_launcher_init(crt);
}

void alt_launcher_respawn(void)
{
	restart_launcher(load_persisted_native_crt());
}

void alt_launcher_set_enabled(bool enabled)
{
	// A redundant "on" must not tear down and respawn a running frontend.
	if (alt_launcher_enabled() == enabled) return;
	if (!zaparoo_settings_set_frontend_enabled(enabled)) return;
	// An updater may have installed the frontend after Main started.
	alt_launcher_installed_refresh();
	printf("alt_launcher: frontend %s\n", enabled ? "enabled" : "disabled");

	if (!enabled)
	{
		stop_launcher(true);
		return;
	}

	// Only the launcher core hosts the frontend; enabling over a game core
	// must not fork it on top of the running game.
	if (alt_launcher_installed() && zaparoo_is_native_core()) alt_launcher_respawn();
}

void alt_launcher_toggle_native_crt(void)
{
	// The OSD toggle. Same contract as the frontend's in-app toggle: flip
	// byte 0 of the state file (byte 1 - the video standard - is preserved),
	// then respawn the launcher under the new setting.
	uint8_t v[2] = {0, 0};
	FileLoadConfig(s_crt_state_file, v, sizeof(v));
	if (v[1] > 2) v[1] = 0;
	v[0] = v[0] ? 0 : 1;
	if (!FileSaveConfig(s_crt_state_file, v, sizeof(v)))
	{
		printf("alt_launcher: OSD CRT toggle: could not write %s\n", s_crt_state_file);
		return;
	}
	printf("alt_launcher: OSD CRT toggle -> enabled=%d mode=%d\n", v[0], v[1]);
	restart_launcher(v[0] != 0);
}

void alt_launcher_set_native_crt_mode(uint8_t mode)
{
	// Byte 1 of the state file sizes the framebuffer on this side; the
	// frontend reads the same standard from frontend.toml, so both are
	// written before the respawn.
	uint8_t v[2] = {0, 0};
	FileLoadConfig(s_crt_state_file, v, sizeof(v));
	if (mode > 2) mode = 0;
	v[1] = mode;
	if (!FileSaveConfig(s_crt_state_file, v, sizeof(v)))
	{
		printf("alt_launcher: OSD video standard: could not write %s\n", s_crt_state_file);
		return;
	}
	crt_toml_set_standard(mode);
	printf("alt_launcher: OSD video standard -> mode=%d (enabled=%d)\n", mode, v[0]);
	if (v[0]) restart_launcher(true);
}

bool zaparoo_is_native_core(void)
{
	static const char *name = "Zaparoo Launcher";
	return !strcasecmp(user_io_get_core_name(0), name) ||
	       !strcasecmp(user_io_get_core_name(1), name);
}

void zaparoo_alt_launcher_init_for_core(void)
{
	if (alt_launcher_configured() && zaparoo_is_native_core())
	{
		printf("alt_launcher: initializing CRT mode for core '%s' '%s'\n",
		       user_io_get_core_name(1), user_io_get_core_name(0));
		alt_launcher_init(true);
	}
}

static void zaparoo_alt_launcher_prepare_menu_state(void)
{
	bool crt = load_persisted_native_crt();
	printf("alt_launcher: initializing menu frontend (persisted crt=%d)\n", crt);
	alt_launcher_init(crt);
}

void zaparoo_alt_launcher_init_for_menu(void)
{
	zaparoo_alt_launcher_prepare_menu_state();
}

void zaparoo_alt_launcher_start_for_menu(void)
{
	// This hook runs before video_init(). Queue startup instead of forking
	// immediately so EDID-selected output geometry is final before the frontend
	// asks Main to choose a full or half framebuffer.
	zaparoo_alt_launcher_prepare_menu_state();
}
