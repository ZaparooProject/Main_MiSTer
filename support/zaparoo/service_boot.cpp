#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static const char s_service_script[] = "/media/fat/Scripts/zaparoo.sh";

// A tty is line-buffered already, but stdout redirected to a file is not, so
// diagnostics sit in the buffer and a captured log reads as empty or truncated.
// Set it once here, before main() and before anything prints, so plain printf
// works everywhere including upstream's own output. Declared before the service
// constructor so it runs first within this file.
__attribute__((constructor)) static void zaparoo_stdout_linebuf(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
}

// Runs before main(): ahead of the core-1 affinity pin (a forked child would
// inherit it) and ahead of FindStorage(), which can block 30s waiting for USB.
// The script is an ensure, so re-running it on every app_restart() is a no-op.
__attribute__((constructor)) static void zaparoo_service_boot(void)
{
	if (access(s_service_script, X_OK)) return;

	pid_t pid = fork();
	if (pid < 0) return;
	if (pid)
	{
		waitpid(pid, NULL, 0);
		return;
	}

	// Double fork + setsid: no PR_SET_PDEATHSIG here, the service must outlive
	// the app_restart() re-exec that every core load performs.
	if (fork()) _exit(0);
	setsid();
	signal(SIGCHLD, SIG_DFL);

	int null = open("/dev/null", O_RDWR);
	if (null >= 0)
	{
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		if (null > STDERR_FILENO) close(null);
	}

	execl(s_service_script, s_service_script, "-service", "start", NULL);
	_exit(127);
}
