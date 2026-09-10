#include "core_watchdog.h"
#include "core_identity.h"
#include "alt_launcher.h"
#include "service_boot.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hardware.h"

static const char s_core_pid_path[] = "/tmp/zaparoo/core.pid";
static unsigned long s_check_timer = 0;
static unsigned long s_restart_timer = 0;

#define CORE_WATCHDOG_POLL_MS 2000
#define CORE_WATCHDOG_RETRY_MS 30000

static int read_core_pid(void)
{
	FILE *fp = fopen(s_core_pid_path, "rt");
	if (!fp) return 0;
	int pid = 0;
	if (fscanf(fp, "%d", &pid) != 1 || pid <= 1) pid = 0;
	fclose(fp);
	return pid;
}

static bool pid_matches_core(int pid)
{
	char path[64], exe[PATH_MAX];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	ssize_t len = readlink(path, exe, sizeof(exe) - 1);
	if (len <= 0 || len >= (ssize_t)sizeof(exe) - 1) return false;
	exe[len] = 0;
	const char deleted[] = " (deleted)";
	if (len >= (ssize_t)sizeof(deleted) - 1 &&
	    !strcmp(exe + len - (sizeof(deleted) - 1), deleted))
		exe[len - (sizeof(deleted) - 1)] = 0;
	if (zaparoo_core_identity::service_binary(exe)) return true;
	if (!zaparoo_core_identity::shell_binary(exe)) return false;

	// Shell-backed service caches name the script in argv[1], not exe.
	// Only complete NUL-delimited arguments count; bound work on Main's loop.
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	FILE *fp = fopen(path, "re");
	if (!fp) return false;
	char args[4096];
	size_t count = fread(args, 1, sizeof(args), fp);
	bool failed = ferror(fp);
	fclose(fp);
	if (failed) return false;
	unsigned argument = 0;
	for (size_t start = 0, i = 0; i < count; ++i)
	{
		if (!args[i])
		{
			if (argument == 1) return zaparoo_core_identity::service_binary(args + start);
			++argument;
			start = i + 1;
		}
	}
	return false;
}

static long memory_available_kib(void)
{
	FILE *fp = fopen("/proc/meminfo", "rt");
	if (!fp) return -1;
	char key[64];
	long value;
	char unit[16];
	long available = -1;
	while (fscanf(fp, "%63s %ld %15s", key, &value, unit) == 3)
	{
		if (!strcmp(key, "MemAvailable:"))
		{
			available = value;
			break;
		}
	}
	fclose(fp);
	return available;
}

void zaparoo_core_watchdog_poll(void)
{
	if (!alt_launcher_active()) return;

	if (!s_check_timer)
	{
		s_check_timer = GetTimer(CORE_WATCHDOG_POLL_MS);
		if (!s_check_timer) s_check_timer = 1;
		return;
	}
	if (!CheckTimer(s_check_timer)) return;
	s_check_timer = GetTimer(CORE_WATCHDOG_POLL_MS);
	if (!s_check_timer) s_check_timer = 1;

	int pid = read_core_pid();
	// Missing PID means an intentional stop or a start that has not written it
	// yet. SIGKILL and OOM can leave the old PID behind; that stale file is
	// a recovery hint, not proof of why the process exited.
	if (!pid) return;
	int result = kill(pid, 0);
	int probe_error = errno;
	if (result && probe_error != EPERM && probe_error != ESRCH) return;
	if ((!result || probe_error == EPERM) && pid_matches_core(pid))
	{
		s_restart_timer = 0;
		return;
	}
	// Recheck identity even on the first observation after Main re-exec.
	// Core owns stale-PID cleanup under its start gate; Main never unlinks it.
	if (s_restart_timer && !CheckTimer(s_restart_timer)) return;

	printf("zaparoo_core_watchdog: Core pid %d missing or identity unverified, ensuring service (MemAvailable=%ld KiB)\n",
	       pid, memory_available_kib());
	zaparoo_service_start_async();
	s_restart_timer = GetTimer(CORE_WATCHDOG_RETRY_MS);
	if (!s_restart_timer) s_restart_timer = 1;
}
