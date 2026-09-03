#pragma once

#include <stdbool.h>

// zaparoo_mount <pos> [path]: swap an image in the running core with no core
// reload, so multi-disk games can change disk. With no path, ejects.
//
// pos is 1-based and counts the core's mountable slots in the order it
// declares them, so 1 is always the first swappable slot whatever the core
// calls it internally. That keeps a card portable across cores.
bool zaparoo_mount(int pos, const char *path);
