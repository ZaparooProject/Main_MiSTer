// Persistent Main log for beta diagnostics, enabled by the presence of
// /media/fat/zaparoo/main.log (touch to enable, delete to disable). stdout and
// stderr are appended to it from before main(), so the fd survives every
// app_restart() re-exec and a reboot, and a heartbeat thread stamps the stream
// with uptime so upstream's untimed output can be placed against zlog's t=.
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char s_log_path[] = "/media/fat/zaparoo/main.log";
static const char s_log_old_path[] = "/media/fat/zaparoo/main.log.old";
static const off_t s_log_rotate_bytes = 8 << 20;
static const unsigned s_heartbeat_ms = 250;

static unsigned long uptime_ms(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_BOOTTIME, &tp);
	return tp.tv_sec * 1000UL + tp.tv_nsec / 1000000UL;
}

static void *heartbeat(void *)
{
	for (;;)
	{
		usleep(s_heartbeat_ms * 1000);
		dprintf(STDOUT_FILENO, "[zt %lu]\n", uptime_ms());
	}
	return NULL;
}

// dprintf, not printf: the line-buffering setvbuf in service_boot.cpp may not
// have run yet, and setvbuf is only valid before the stream's first use.
__attribute__((constructor)) static void zaparoo_main_log(void)
{
	struct stat st;
	if (stat(s_log_path, &st)) return;
	if (st.st_size > s_log_rotate_bytes) rename(s_log_path, s_log_old_path);

	int fd = open(s_log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd < 0) return;
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > STDERR_FILENO) close(fd);

	char now[32] = "";
	time_t t = time(NULL);
	strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", localtime(&t));
	dprintf(STDOUT_FILENO, "\n==== MiSTer_Zaparoo start pid=%d uptime=%lu ms clock=%s ====\n", getpid(), uptime_ms(), now);

	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thread, &attr, heartbeat, NULL);
	pthread_attr_destroy(&attr);
}
