#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Tells Zaparoo Core which zaparoo_ commands this Main accepts on
// /dev/MiSTer_cmd. Stock Main ignores those lines without a reply, so Core
// cannot probe for them. The line is "<version> <pid> <command>...": Core
// treats the file as current only while the pid is alive. app_restart()
// starts a new process on every core load, and that process rewrites the
// file with its own pid, so a Main without this file (a stock binary picked
// by a core's main=) leaves a dead pid behind and is never mistaken for this
// one.
static const char s_capabilities_path[] = "/tmp/zaparoo_main";
static const char s_capabilities_tmp_path[] = "/tmp/zaparoo_main.tmp";
static const char s_capabilities[] = "cheat save mount osd kiosk frontend";

// A constructor so the file exists before main() can block in FindStorage().
// /tmp is tmpfs and mounted long before Main starts.
__attribute__((constructor)) static void zaparoo_publish_capabilities(void)
{
	FILE *fp = fopen(s_capabilities_tmp_path, "w");
	if (!fp)
	{
		printf("zaparoo: unable to write %s: %s\n", s_capabilities_tmp_path, strerror(errno));
		return;
	}

	int written = fprintf(fp, "1 %d %s\n", getpid(), s_capabilities);
	if (fclose(fp) || written < 0)
	{
		unlink(s_capabilities_tmp_path);
		return;
	}
	if (rename(s_capabilities_tmp_path, s_capabilities_path))
	{
		printf("zaparoo: unable to replace %s: %s\n", s_capabilities_path, strerror(errno));
		unlink(s_capabilities_tmp_path);
	}
}
