#include "service_boot.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static const char s_service_script[] = "/media/fat/Scripts/zaparoo.sh";
static cpu_set_t s_service_affinity;
static bool s_service_affinity_valid = false;

// A tty is line-buffered already, but stdout redirected to a file is not, so
// diagnostics sit in the buffer and a captured log reads as empty or truncated.
// Set it once here, before main() and before anything prints, so plain printf
// works everywhere including upstream's own output. Declared before the service
// constructor so it runs first within this file.
__attribute__((constructor)) static void zaparoo_stdout_linebuf(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
}

void zaparoo_service_start_async(void)
{
	if (access(s_service_script, X_OK)) return;

	pid_t pid = fork();
	if (pid < 0) return;
	if (pid)
	{
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
		return;
	}

	// Double fork + setsid: no PR_SET_PDEATHSIG here, the service must outlive
	// the app_restart() re-exec that every core load performs.
	if (fork()) _exit(0);
	setsid();
	signal(SIGCHLD, SIG_DFL);
	// A watchdog restart happens after Main pins itself to CPU 1. Restore
	// the startup mask so Core does not inherit that latency-sensitive pin.
	if (s_service_affinity_valid)
		sched_setaffinity(0, sizeof(s_service_affinity), &s_service_affinity);

	int null = open("/dev/null", O_RDWR);
	if (null >= 0)
	{
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		if (null > STDERR_FILENO) close(null);
	}

	// Unlike the boot constructor, watchdog calls run after Main has opened
	// input and hardware devices. None of those descriptors belong to Core.
	DIR *fds = opendir("/proc/self/fd");
	if (!fds) _exit(127);
	struct dirent *entry;
	while ((entry = readdir(fds)))
	{
		char *end;
		long fd = strtol(entry->d_name, &end, 10);
		if (!*end && fd > STDERR_FILENO && fd != dirfd(fds)) close((int)fd);
	}
	closedir(fds);

	execl(s_service_script, s_service_script, "-service", "start", NULL);
	_exit(127);
}

// Runs before main(): ahead of the core-1 affinity pin (a forked child would
// inherit it) and ahead of FindStorage(), which can block 30s waiting for USB.
// The script is an ensure, so re-running it on every app_restart() is a no-op.
__attribute__((constructor)) static void zaparoo_service_boot(void)
{
	s_service_affinity_valid = !sched_getaffinity(0, sizeof(s_service_affinity), &s_service_affinity);
	zaparoo_service_start_async();
}
