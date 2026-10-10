#ifndef ZAPAROO_SCANOUT_CONFLICT_H
#define ZAPAROO_SCANOUT_CONFLICT_H

#include <sys/types.h>

namespace zaparoo_scanout {

inline bool internal_mapping_process(long pid, pid_t main_pid, pid_t frontend_pid, pid_t loader_pid)
{
	// The loader only hashes and loads our module. Before exec it inherits
	// Main's mappings; those are not an independent physical-memory client.
	return pid > 0 && (pid == main_pid || pid == frontend_pid || pid == loader_pid);
}

// Physical ranges the module maps: the HDMI scanout slots and the Menu core's
// native video window. limit is one past the last mapped byte.
inline bool conflicting_physical_range(unsigned long long offset, unsigned long long limit)
{
	return (offset < 0x23800000ULL && limit > 0x23000000ULL) ||
	       (offset < 0x3A300000ULL && limit > 0x3A000000ULL);
}

}
#endif
