#pragma once

// Matches Core's daemon.serviceCachePathValid contract for MiSTer's data dir.
// This identifies a cooperative service binary, not a security boundary. Core
// must recheck identity under its start gate before removing stale PID state.
namespace zaparoo_core_identity {
constexpr bool starts_with(const char *value, const char *prefix)
{
	while (*prefix) if (*value++ != *prefix++) return false;
	return true;
}

constexpr bool equals(const char *value, const char *expected)
{
	while (*value && *value == *expected) { ++value; ++expected; }
	return *value == *expected;
}

constexpr bool service_binary(const char *path)
{
	const char prefix[] = "/media/fat/zaparoo/zaparoo.";
	if (!path || !starts_with(path, prefix)) return false;
	const char *name = path + sizeof(prefix) - 1;
	if (equals(name, "service") || equals(name, "service.sh")) return true;
	for (unsigned i = 0; i < 16; ++i)
	{
		char c = *name++;
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	}
	return !*name || equals(name, ".sh");
}

constexpr bool shell_binary(const char *path)
{
	const char *name = path;
	for (const char *p = path; *p; ++p) if (*p == '/') name = p + 1;
	return equals(name, "sh") || equals(name, "bash") ||
		equals(name, "dash") || equals(name, "busybox");
}

static_assert(shell_binary("/bin/sh") && shell_binary("/bin/busybox"), "service interpreters");
static_assert(!shell_binary("/usr/bin/tail"), "data arguments are not identity");
static_assert(service_binary("/media/fat/zaparoo/zaparoo.service"), "legacy service");
static_assert(service_binary("/media/fat/zaparoo/zaparoo.service.sh"), "legacy script");
static_assert(service_binary("/media/fat/zaparoo/zaparoo.0123456789abcdef.sh"), "cached script");
static_assert(service_binary("/media/fat/zaparoo/zaparoo.0123456789abcdef"), "cached ELF");
static_assert(!service_binary("/bin/sleep"), "foreign executable");
static_assert(!service_binary("/media/fat/zaparoo/nested/zaparoo.service"), "direct child only");
static_assert(!service_binary("/tmp/zaparoo.service"), "data directory boundary");
static_assert(!service_binary("/media/fat/zaparoo/zaparoo.0123456789abcdeg.sh"), "hex hash only");
static_assert(!service_binary("/media/fat/zaparoo/zaparoo.0123456789abcdef.bin"), "exact suffix");
static_assert(!service_binary("/media/fat/zaparoo/zaparoo.service/other"), "exact filename");
static_assert(!service_binary("/media/fat/zaparoo/zaparoo."), "missing hash");
static_assert(!service_binary("/media/fat/zaparoo/zaparoo.0123"), "short hash");
static_assert(!service_binary(nullptr), "missing path");
}
