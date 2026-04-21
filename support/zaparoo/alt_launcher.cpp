#include "alt_launcher.h"
#include <errno.h>
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

static void spawn(void)
{
	char path[2100];
	strncpy(path, getFullPath(cfg.alt_launcher), sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	static const char cmd[] =
		"#!/bin/bash\nexport LC_ALL=en_US.UTF-8\nexport HOME=/root\nexec \"$ALT_LAUNCHER_PATH\"\n";

	unlink("/tmp/alt_launcher");
	if (!FileSave("/tmp/alt_launcher", (void*)cmd, strlen(cmd)))
		return;

	setenv("ALT_LAUNCHER_PATH", path, 1);

	user_io_osd_key_enable(0);
	video_chvt(2);
	video_fb_enable(1);

	s_pid = fork();
	if (s_pid < 0)
	{
		printf("alt_launcher: fork failed: %s\n", strerror(errno));
		s_pid = 0;
		user_io_osd_key_enable(1);
		video_fb_enable(0);
		video_chvt(1);
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
		      "/tmp/alt_launcher", "--nohostname", "-L", "tty2", "linux", NULL);
		_exit(1);
	}
}

void alt_launcher_init(void)
{
	if (!cfg.alt_launcher[0] || !cfg.fb_terminal || s_pid || s_gave_up)
		return;
	s_crash_count = 0;
	s_respawn_timer = 0;
	spawn();
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
			bool crashed = WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
			if (crashed && ++s_crash_count >= 3)
			{
				printf("alt_launcher: giving up after 3 crashes\n");
				video_fb_enable(0);
				video_chvt(1);
				s_gave_up = true;
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

	if (s_respawn_timer && CheckTimer(s_respawn_timer))
	{
		s_respawn_timer = 0;
		spawn();
	}
}

void alt_launcher_shutdown(void)
{
	if (!s_pid)
		return;

	kill(s_pid, SIGTERM);
	for (int i = 0; i < 50; i++)
	{
		if (waitpid(s_pid, NULL, WNOHANG) == s_pid)
		{
			s_pid = 0;
			break;
		}
		usleep(10000);
	}
	if (s_pid)
	{
		kill(s_pid, SIGKILL);
		waitpid(s_pid, NULL, 0);
		s_pid = 0;
	}
	video_fb_enable(0);
	video_chvt(1);
}
