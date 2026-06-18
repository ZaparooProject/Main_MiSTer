#ifndef ZAPAROO_LAUNCHER_INPUT_METADATA_H
#define ZAPAROO_LAUNCHER_INPUT_METADATA_H

#include <stddef.h>
#include <stdint.h>
#include "../../input.h" // for NUMBUTTONS

struct launcher_input_snapshot
{
	char     name[128];
	char     idstr[256];
	uint16_t vid;
	uint16_t pid;
	uint32_t unique_hash;
	uint32_t mmap[NUMBUTTONS];

	bool     kbd_present;
	char     kbd_name[128];
	uint16_t kbd_vid;
	uint16_t kbd_pid;
};

// active_source: "controller" | "keyboard".
void launcher_input_metadata_write(const launcher_input_snapshot *snap, int player, const char *active_source);

// Writes the file at launcher start; defined in input.cpp.
void input_export_launcher_metadata(void);

#endif // ZAPAROO_LAUNCHER_INPUT_METADATA_H
