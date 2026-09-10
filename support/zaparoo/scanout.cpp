#include "scanout.h"
#include "../../spi.h"
#include "../../user_io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace zaparoo_scanout {
namespace {
const char request[] = "ZAPAROO-SCANOUT-1";
const char granted[] = "ZAPAROO-SCANOUT-1 OK";
const char rejected[] = "ZAPAROO-SCANOUT-1 NO";
const char device[] = "/dev/zaparoo-scanout";
const char module[] = "/media/fat/zaparoo/modules/6.18.38-MiSTer/zaparoo_scanout.ko";
int parent_fd = -1;
int child_fd = -1;
pid_t frontend_pid = 0;
pid_t owner_pid = 0;
pid_t loader_pid = 0;
bool requested = false;
bool granted_lease = false;
bool loader_tried = false;
unsigned long long loader_started = 0;

unsigned long long now_ms()
{
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void close_fd(int &fd)
{
	if (fd >= 0) close(fd);
	fd = -1;
}

bool device_ready()
{
	struct stat st;
	return !stat(device, &st) && S_ISCHR(st.st_mode) && !access(device, R_OK | W_OK);
}

// Raw WC mappers do not participate in request_mem_region_exclusive. Check
// active clients, not merely loaded module names. This is cooperative conflict
// avoidance, not protection against a privileged process racing a new mapping.
bool conflicting_mappings()
{
	DIR *dir = opendir("/proc");
	if (!dir) return true;
	bool conflict = false;
	struct dirent *entry;
	while (!conflict && (entry = readdir(dir)))
	{
		char *end;
		long pid = strtol(entry->d_name, &end, 10);
		if (!pid || *end || pid == getpid() || pid == frontend_pid) continue;
		char path[80];
		snprintf(path, sizeof(path), "/proc/%ld/maps", pid);
		FILE *maps = fopen(path, "re");
		if (!maps)
		{
			if (errno != ENOENT && errno != ESRCH) conflict = true;
			continue;
		}
		char line[2048];
		while (fgets(line, sizeof(line), maps))
		{
			if (strstr(line, "/dev/mem_wc") || strstr(line, "/dev/mister-magik-scanout-slots") ||
			    strstr(line, device))
			{
				conflict = true;
				break;
			}
			if (strstr(line, "/dev/mem"))
			{
				unsigned long long start, end_addr, offset;
				if (sscanf(line, "%llx-%llx %*4s %llx", &start, &end_addr, &offset) != 3 ||
				    end_addr < start || offset + (end_addr - start) < offset)
				{
					conflict = true;
					break;
				}
				unsigned long long limit = offset + end_addr - start;
				if (offset < 0x23800000ULL && limit > 0x23000000ULL)
				{
					conflict = true;
					break;
				}
			}
		}
		if (ferror(maps)) conflict = true;
		fclose(maps);
	}
	closedir(dir);
	return conflict;
}

void deny(const char *reason)
{
	printf("zaparoo_scanout: fb0 fallback: %s\n", reason);
	if (parent_fd >= 0) send(parent_fd, rejected, sizeof(rejected) - 1, MSG_NOSIGNAL);
	close_fd(parent_fd);
	requested = false;
	granted_lease = false;
}

bool ensure_module()
{
	if (device_ready()) return true;
	if (loader_pid)
	{
		int status = 0;
		pid_t result = waitpid(loader_pid, &status, WNOHANG);
		if (result == loader_pid || (result < 0 && errno == ECHILD))
		{
			loader_pid = 0;
			if (device_ready()) return true;
			deny("module load failed");
		}
		else if (now_ms() - loader_started > 3000)
		{
			kill(loader_pid, SIGKILL);
			deny("module load timed out");
		}
		return false;
	}
	if (loader_tried) return false;
	loader_tried = true;
	if (access(module, R_OK))
	{
		deny("qualified kernel module missing");
		return false;
	}
	loader_started = now_ms();
	loader_pid = fork();
	if (loader_pid < 0)
	{
		loader_pid = 0;
		deny("module loader fork failed");
	}
	else if (!loader_pid)
	{
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (getppid() != owner_pid) _exit(127);
		execl("/sbin/insmod", "insmod", module, NULL);
		_exit(127);
	}
	return false;
}
}

void stop()
{
	close_fd(parent_fd);
	close_fd(child_fd);
	frontend_pid = 0;
	requested = false;
	granted_lease = false;
	if (loader_pid)
	{
		kill(loader_pid, SIGKILL);
		int status;
		pid_t result = waitpid(loader_pid, &status, WNOHANG);
		if (result == loader_pid || (result < 0 && errno == ECHILD)) loader_pid = 0;
	}
}

void prepare(bool eligible)
{
	stop();
	owner_pid = getpid();
	loader_tried = false;
	struct utsname kernel;
	if (!eligible || loader_pid || uname(&kernel) || strcmp(kernel.release, "6.18.38-MiSTer")) return;
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets)) return;
	parent_fd = sockets[0];
	child_fd = sockets[1];
}

void child_environment()
{
	if (getppid() != owner_pid) _exit(1);
	unsetenv("ZAPAROO_SCANOUT_FD");
	close_fd(parent_fd);
	if (child_fd < 0) return;
	int flags = fcntl(child_fd, F_GETFD);
	if (flags < 0 || fcntl(child_fd, F_SETFD, flags & ~FD_CLOEXEC))
	{
		close_fd(child_fd);
		return;
	}
	char value[24];
	snprintf(value, sizeof(value), "%d", child_fd);
	setenv("ZAPAROO_SCANOUT_FD", value, 1);
}

void parent_started(pid_t pid)
{
	close_fd(child_fd);
	frontend_pid = pid;
}

bool owned() { return granted_lease; }
bool offered() { return parent_fd >= 0; }

bool bootstrap_available()
{
	struct utsname kernel;
	return !uname(&kernel) && !strcmp(kernel.release, "6.18.38-MiSTer") &&
		(device_ready() || !access(module, R_OK));
}

bool blank_framebuffer()
{
	if (granted_lease) return false;
	DisableIO();
	int supported = spi_uio_cmd_cont(UIO_SET_FBUF);
	spi_w(0);
	DisableIO();
	return supported != 0;
}

bool poll(bool video_ready)
{
	// Reap our own loader even when a denied/disconnected request is gone.
	if (loader_pid && (parent_fd < 0 || granted_lease))
	{
		int status;
		pid_t result = waitpid(loader_pid, &status, WNOHANG);
		if (result == loader_pid || (result < 0 && errno == ECHILD)) loader_pid = 0;
	}
	if (parent_fd < 0) return false;
	char buffer[64];
	ssize_t count = recv(parent_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
	if (!count || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
	{
		stop();
		return false;
	}
	if (count > 0 && granted_lease)
	{
		// A protocol error cannot safely revoke a live writer's ownership.
		// Keep abstaining until its descriptor closes or Main stops it.
		return false;
	}
	if (count > 0)
	{
		if (requested || count != (ssize_t)sizeof(request) - 1 ||
		    memcmp(buffer, request, sizeof(request) - 1))
		{
			deny("invalid ownership request");
			return false;
		}
		requested = true;
	}
	if (!requested || granted_lease || !video_ready) return false;
	if (conflicting_mappings())
	{
		deny("another physical-memory client is active");
		return false;
	}
	if (!ensure_module()) return false;
	// Main is single-threaded/cooperatively scheduled: no FPGA work may occur
	// between this ownership flag and acknowledgment, or until disconnect.
	granted_lease = true;
	if (send(parent_fd, granted, sizeof(granted) - 1, MSG_NOSIGNAL) != (ssize_t)sizeof(granted) - 1)
	{
		stop();
		return false;
	}
	printf("zaparoo_scanout: frontend pid=%d owns the FPGA bus\n", frontend_pid);
	return true;
}
}
