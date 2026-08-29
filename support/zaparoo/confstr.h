#pragma once

#include <stdbool.h>

// Enumerates the running core's mountable slots in the order it declares them,
// which is the order they appear in its own menu. Callers address a slot by
// 1-based position, not by the core's raw slot number, so one command works
// across cores that number their slots differently.
//
// Hidden and disabled slots are counted too: the position must stay stable
// when an unrelated core option changes what the menu shows.
int zaparoo_confstr_slot_count(void);

// pos is 1-based. Any output pointer may be null. core_slot receives the
// core's own slot number, ext the extension list space-padded to a multiple of
// 3 (the form the file helpers expect), label the menu text.
bool zaparoo_confstr_slot(int pos, int *core_slot,
                          char *ext, int ext_size,
                          char *label, int label_size);
