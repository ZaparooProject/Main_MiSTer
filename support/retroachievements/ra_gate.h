#pragma once

#include <string.h>
#include <strings.h>

// RA cores are launched by the RA installer's MGLs (setname RA_<core>, rbf in
// _RA_Cores/Cores). Standard cores share base names with them, and the RA
// handlers write hardcore status bits on init, so nothing else may enable RA.
static inline bool ra_gate_is_ra_core(const char *core_name, const char *rbf_path)
{
	if (core_name && !strncasecmp(core_name, "RA_", 3)) return true;
	if (rbf_path && strcasestr(rbf_path, "_RA_Cores/")) return true;
	return false;
}

// The RA installer routes RA cores to this binary through [RA_*] main=.
static inline bool ra_gate_is_ra_main(const char *exe)
{
	if (!exe) return false;
	const char *base = strrchr(exe, '/');
	base = base ? base + 1 : exe;
	return !strcmp(base, "MiSTer_RA");
}
