#include "alt_launcher.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "cfg.h"
#include "file_io.h"
#include "hardware.h"
#include "input.h"
#include "menu.h"
#include "shmem.h"
#include "user_io.h"
#include "video.h"

static const char s_launcher_path[] = "zaparoo/launcher";

void alt_launcher_cfg_apply(void)
{
	// Override any user ini values: this fork is single-purpose and the
	// launcher needs both flags on to render.
	cfg.fb_terminal = 1;
	cfg.recents = 1;
}

static bool s_escaped = false;

bool alt_launcher_configured(void)
{
	// After a clean exit / give-up, masquerade as not-configured so the rest
	// of the OSD reverts to stock menu behavior for the rest of this session.
	// Reboot re-execs MiSTer and resets this back to the file-existence cache.
	if (s_escaped) return false;
	static int cached = -1;
	if (cached < 0) cached = FileExists(s_launcher_path, 0) ? 1 : 0;
	return cached != 0;
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

static pid_t s_pid = 0;
static int s_crash_count = 0;
static unsigned long s_respawn_timer = 0;
static unsigned long s_native_status_timer = 0;
static unsigned long s_native_fb_mode_timer = 0;
static bool s_gave_up = false;
static bool s_init_pending = false;
static bool s_native_crt = false;
static const int s_vt = 2;
static const char s_tty[] = "tty2";
static const char s_tty_path[] = "/dev/tty2";
static const char s_fb_mode_path[] = "/sys/module/MiSTer_fb/parameters/mode";
static const char s_crt_state_file[] = "zaparoo_launcher_crt.bin";
static const char s_offsets_file[] = "zaparoo_video_offsets.bin";

static int8_t s_h_offset = 0;
static int8_t s_v_offset = 0;

static int8_t clamp_offset(int8_t v)
{
	if (v < -8) return -8;
	if (v > 7) return 7;
	return v;
}

static bool load_persisted_native_crt(void)
{
	uint8_t v = 0;
	FileLoadConfig(s_crt_state_file, &v, sizeof(v));
	return v != 0;
}

static void save_persisted_native_crt(bool crt)
{
	uint8_t v = crt ? 1 : 0;
	FileSaveConfig(s_crt_state_file, &v, sizeof(v));
}

static void load_persisted_offsets(void)
{
	int8_t buf[2] = { 0, 0 };
	FileLoadConfig(s_offsets_file, buf, sizeof(buf));
	s_h_offset = clamp_offset(buf[0]);
	s_v_offset = clamp_offset(buf[1]);
}

static void save_persisted_offsets(void)
{
	int8_t buf[2] = { s_h_offset, s_v_offset };
	FileSaveConfig(s_offsets_file, buf, sizeof(buf));
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

static void set_native_crt_fb_mode(bool log = true)
{
	set_launcher_fb_mode(8888, 1, 320, 240, 1280, log);
}

static void blank_native_crt_fb(void)
{
	// CRT path doesn't read /dev/fb0 (kernel FB at 0x22000000). The launcher
	// runs a worker that copies the top-left 320x240 from /dev/fb0 into a
	// separate FPGA-mapped region at 0x3A000000 (control word + two 320x240
	// RGBA buffers). The FPGA scans out from that region. Nothing zeros it
	// across launcher restarts or software reboots, so the previous session's
	// last frame ghosts in until the launcher's writer thread starts.
	const uint32_t native_addr = 0x3A000000u;
	const uint32_t native_size = 0x000A0000u;
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
	user_io_status_set("[9]", 0);
	video_fb_enable(0);
	set_vga_fb(0);
	set_launcher_fb_mode(8888, 1, 960, 720, 3840);
	s_native_status_timer = 0;
	s_native_fb_mode_timer = 0;
}

static void enable_native_crt_path(void)
{
	set_vga_fb(0);
	video_fb_enable(0);

	// Double-write with a settle window so the kernel module's 320x240 layout
	// is live before status[9] flips. Without this, the launcher renders for
	// up to a second under stale dims (the post-fork retry timer used to be
	// what eventually fixed the picture).
	set_native_crt_fb_mode(false);
	usleep(100000);
	set_native_crt_fb_mode();

	blank_native_crt_fb();

	user_io_status_set("[9]", 1);
	s_native_status_timer = GetTimer(500);
	if (!s_native_status_timer) s_native_status_timer = 1;
	s_native_fb_mode_timer = GetTimer(1000);
	if (!s_native_fb_mode_timer) s_native_fb_mode_timer = 1;
}

static void wait_launcher_tty_ready(pid_t pid)
{
	for (int i = 0; i < 100; i++)
	{
		if (launcher_tty_ready(pid))
			return;
		usleep(10000);
	}
}

static void return_to_normal_mode(void)
{
	user_io_osd_key_enable(1);
	reset_launcher_tty();
	video_menu_bg(user_io_status_get("[3:1]"));
	if (s_native_crt) disable_native_crt_path();
	else video_fb_enable(0);
	s_native_crt = false;
	s_respawn_timer = 0;
	s_crash_count = 0;
	s_gave_up = true;
	s_escaped = true;
}

static void reset_launcher_state(void)
{
	s_pid = 0;
	s_respawn_timer = 0;
	s_crash_count = 0;
	s_gave_up = false;
	s_init_pending = false;
	s_escaped = false;
}

static void kill_launcher(pid_t pid, int sig)
{
	if (kill(-pid, sig) && errno == ESRCH)
		kill(pid, sig);
}

static void spawn(void)
{
	char path[2100];
	strncpy(path, getFullPath(s_launcher_path), sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	static const char cmd[] =
		"#!/bin/bash\n"
		"export LC_ALL=en_US.UTF-8\n"
		"export HOME=/root\n"
		"printf '\\033[0m\\033[?25l\\033[37m\\033[40m\\033[2J\\033[H'\n"
		"if [ \"$ALT_LAUNCHER_CRT\" = \"1\" ]; then\n"
		"	exec \"$ALT_LAUNCHER_PATH\" --crt\n"
		"fi\n"
		"exec \"$ALT_LAUNCHER_PATH\"\n";

	unlink("/tmp/alt_launcher");
	if (!FileSave("/tmp/alt_launcher", (void*)cmd, strlen(cmd)))
		return;

	user_io_osd_key_enable(0);
	clear_launcher_tty();

	printf("alt_launcher: native_crt=%d\n", s_native_crt);
	if (s_native_crt)
	{
		enable_native_crt_path();
		printf("alt_launcher: native CRT path enabled\n");
	}
	else
	{
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
	printf("alt_launcher: spawned pid=%d path=%s\n", s_pid, path);
	if (!s_pid)
	{
		setenv("ALT_LAUNCHER_PATH", path, 1);
		setenv("ALT_LAUNCHER_CRT", s_native_crt ? "1" : "0", 1);
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(0, &set);
		sched_setaffinity(0, sizeof(set), &set);
		setsid();
		execl("/sbin/agetty", "/sbin/agetty", "-a", "root", "-l",
		      "/tmp/alt_launcher", "-i", "--nohostname", "-L", s_tty, "linux", NULL);
		_exit(1);
	}

	wait_launcher_tty_ready(s_pid);
	video_chvt(s_vt);
	if (!s_native_crt)
		video_fb_enable(1);
	else
	{
		input_switch(0);
		user_io_status_set("[9]", 1);
	}

	// The launcher grabs input as soon as it starts. If the OSD is still
	// up (e.g. user toggled CRT mode or hit Reboot from System Settings),
	// it would trap input with no way to dismiss it — drop it now.
	if (menu_present()) MenuHide();
}

bool alt_launcher_active(void)
{
	return s_pid != 0;
}

bool alt_launcher_native_crt(void)
{
	return s_native_crt && s_pid != 0;
}

int8_t alt_launcher_h_offset(void)
{
	return s_h_offset;
}

int8_t alt_launcher_v_offset(void)
{
	return s_v_offset;
}

void alt_launcher_set_h_offset(int8_t v)
{
	s_h_offset = clamp_offset(v);
	save_persisted_offsets();
	// 4-bit two's-complement bit pattern; FPGA reinterprets as signed -8..+7.
	user_io_status_set("[13:10]", (uint32_t)((uint8_t)s_h_offset & 0xF));
}

void alt_launcher_set_v_offset(int8_t v)
{
	s_v_offset = clamp_offset(v);
	save_persisted_offsets();
	user_io_status_set("[17:14]", (uint32_t)((uint8_t)s_v_offset & 0xF));
}

void alt_launcher_toggle_crt(void)
{
	bool current_crt = alt_launcher_native_crt();
	bool target_crt  = !current_crt;

	save_persisted_native_crt(target_crt);

	printf("alt_launcher: toggle CRT path %d -> %d\n", current_crt, target_crt);

	// Shutdown drops status[9], releases the FB mode and restores HPS framebuffer
	// state regardless of whether the launcher was running. After it returns we
	// always have a clean slate to spawn the next launcher invocation.
	alt_launcher_shutdown();
	alt_launcher_init(target_crt);
}

void alt_launcher_init(bool native_crt)
{
	if (!alt_launcher_configured() || s_pid || s_gave_up)
		return;
	s_crash_count = 0;
	s_respawn_timer = 0;
	s_native_crt = native_crt;
	s_init_pending = true;
}

void alt_launcher_poll(void)
{
	if (s_pid)
	{
		if (s_native_crt && s_native_status_timer && CheckTimer(s_native_status_timer))
		{
			user_io_status_set("[9]", 1);
			s_native_status_timer = GetTimer(500);
			if (!s_native_status_timer) s_native_status_timer = 1;
		}

		if (s_native_crt && s_native_fb_mode_timer && CheckTimer(s_native_fb_mode_timer))
		{
			set_native_crt_fb_mode();
			s_native_fb_mode_timer = 0;
		}

		int status;
		if (waitpid(s_pid, &status, WNOHANG) == s_pid)
		{
			s_pid = 0;
			user_io_osd_key_enable(1);
			bool exited = WIFEXITED(status);
			int exit_status = exited ? WEXITSTATUS(status) : 0;
			int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
			bool escaped = (exited && exit_status == 0) || sig == SIGTERM || sig == SIGINT;
			bool crashed = !escaped && (sig != 0 || (exited && exit_status != 0));
			// Any exit while in CRT mode drops back to HDMI / no launcher
			// for the rest of this session — respawning into CRT after the
			// user already left it is a UX trap. The persisted CRT
			// preference is intentionally untouched (return_to_normal_mode
			// only clears the in-memory s_native_crt), so the next reboot
			// honors whatever the user last set in System Settings.
			if (s_native_crt)
			{
				printf("alt_launcher: exited while in CRT mode, dropping to HDMI normal mode\n");
				return_to_normal_mode();
				return;
			}
			if (escaped)
			{
				printf("alt_launcher: exited, returning to normal mode until restart\n");
				return_to_normal_mode();
				return;
			}
			if (crashed && ++s_crash_count >= 3)
			{
				printf("alt_launcher: giving up after 3 crashes\n");
				return_to_normal_mode();
				return;
			}
			if (!crashed)
				s_crash_count = 0;
			s_respawn_timer = GetTimer(1000);
			if (!s_respawn_timer) s_respawn_timer = 1;
		}
		return;
	}

	if (!alt_launcher_configured())
		return;

	if (s_init_pending)
	{
		s_init_pending = false;
		spawn();
		return;
	}

	if (s_respawn_timer && CheckTimer(s_respawn_timer))
	{
		s_respawn_timer = 0;
		spawn();
	}
}

void alt_launcher_shutdown(void)
{
	if (!s_pid)
	{
		reset_launcher_state();
		if (s_native_crt)
		{
			s_native_crt = false;
			disable_native_crt_path();
		}
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

void zaparoo_alt_launcher_init_for_menu(void)
{
	bool crt = load_persisted_native_crt();
	load_persisted_offsets();
	printf("alt_launcher: initializing menu launcher (persisted crt=%d, h=%d, v=%d)\n",
	       crt, s_h_offset, s_v_offset);
	// Push the persisted offsets to the FPGA now that the menu RBF is loaded.
	user_io_status_set("[13:10]", (uint32_t)((uint8_t)s_h_offset & 0xF));
	user_io_status_set("[17:14]", (uint32_t)((uint8_t)s_v_offset & 0xF));
	alt_launcher_init(crt);
}
