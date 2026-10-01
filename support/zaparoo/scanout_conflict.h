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

}
#endif
