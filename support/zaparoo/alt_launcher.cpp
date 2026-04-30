#include "alt_launcher.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "cfg.h"
#include "file_io.h"
#include "hardware.h"
#include "user_io.h"
#include "video.h"

static pid_t s_pid = 0;
static int s_crash_count = 0;
static unsigned long s_respawn_timer = 0;
static bool s_gave_up = false;
static bool s_init_pending = false;
static const int s_vt = 6;
static const char s_tty[] = "tty6";
static const char s_tty_path[] = "/dev/tty6";

static void clear_launcher_tty(void)
{
	int tty_fd = open(s_tty_path, O_WRONLY | O_CLOEXEC);
	if (tty_fd >= 0)
	{
		static const char blank[] = "\033[?25l\033[40m\033[30m\033[2J\033[H";
		write(tty_fd, blank, sizeof(blank) - 1);
		close(tty_fd);
	}
}

static void return_to_normal_mode(void)
{
	user_io_osd_key_enable(1);
	video_menu_bg(user_io_status_get("[3:1]"));
	video_fb_enable(0);
	s_respawn_timer = 0;
	s_crash_count = 0;
	s_gave_up = true;
}

static void reset_launcher_state(void)
{
	s_pid = 0;
	s_respawn_timer = 0;
	s_crash_count = 0;
	s_gave_up = false;
	s_init_pending = false;
}

static void kill_launcher(pid_t pid, int sig)
{
	if (kill(-pid, sig) && errno == ESRCH)
		kill(pid, sig);
}

static void spawn(void)
{
	char path[2100];
	strncpy(path, getFullPath(cfg.alt_launcher), sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	static const char cmd[] =
		"#!/bin/bash\nexport LC_ALL=en_US.UTF-8\nexport HOME=/root\nprintf '\\033[0m\\033[?25l\\033[37m\\033[40m\\033[2J\\033[H'\nexec \"$ALT_LAUNCHER_PATH\"\n";

	unlink("/tmp/alt_launcher");
	if (!FileSave("/tmp/alt_launcher", (void*)cmd, strlen(cmd)))
		return;

	setenv("ALT_LAUNCHER_PATH", path, 1);

	user_io_osd_key_enable(0);
	clear_launcher_tty();

	s_pid = fork();
	if (s_pid < 0)
	{
		printf("alt_launcher: fork failed: %s\n", strerror(errno));
		s_pid = 0;
		user_io_osd_key_enable(1);
		video_fb_enable(0);
		return;
	}
	printf("alt_launcher: spawned pid=%d path=%s\n", s_pid, path);
	if (!s_pid)
	{
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(0, &set);
		sched_setaffinity(0, sizeof(set), &set);
		setsid();
		execl("/sbin/agetty", "/sbin/agetty", "-a", "root", "-l",
		      "/tmp/alt_launcher", "-i", "--nohostname", "-L", s_tty, "linux", NULL);
		_exit(1);
	}

	usleep(100000);
	video_chvt(s_vt);
	video_fb_enable(1);
}

bool alt_launcher_active(void)
{
	return s_pid != 0;
}

void alt_launcher_init(void)
{
	if (!cfg.alt_launcher[0] || !cfg.fb_terminal || s_pid || s_gave_up)
		return;
	s_crash_count = 0;
	s_respawn_timer = 0;
	s_init_pending = true;
}

void alt_launcher_poll(void)
{
	if (s_pid)
	{
		int status;
		if (waitpid(s_pid, &status, WNOHANG) == s_pid)
		{
			s_pid = 0;
			user_io_osd_key_enable(1);
			bool exited = WIFEXITED(status);
			int exit_status = exited ? WEXITSTATUS(status) : 0;
			int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
			bool escaped = (exited && exit_status == 0) || sig == SIGTERM || sig == SIGINT;
			bool crashed = sig != 0 || (exited && exit_status != 0);
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

	if (!cfg.alt_launcher[0] || !cfg.fb_terminal)
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
		waitpid(pid, NULL, 0);
	}

	reset_launcher_state();
}
