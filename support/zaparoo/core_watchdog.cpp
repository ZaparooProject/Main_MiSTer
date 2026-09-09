#include "core_watchdog.h"
#include "alt_launcher.h"
#include "service_boot.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

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
	if (!kill(pid, 0) || errno == EPERM)
	{
		s_restart_timer = 0;
		return;
	}
	if (errno != ESRCH) return;
	if (s_restart_timer && !CheckTimer(s_restart_timer)) return;

	printf("zaparoo_core_watchdog: Core pid %d disappeared, restarting (MemAvailable=%ld KiB)\n",
	       pid, memory_available_kib());
	zaparoo_service_start_async();
	s_restart_timer = GetTimer(CORE_WATCHDOG_RETRY_MS);
	if (!s_restart_timer) s_restart_timer = 1;
}
