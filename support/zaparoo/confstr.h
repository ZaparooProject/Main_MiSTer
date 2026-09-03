#pragma once

#include <stdbool.h>
#include <stdint.h>

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

// Finds the core's own "Autosave" option, which gates whether it dumps battery
// RAM when OSD_STATUS rises. It ships Off on most cores, so a forced save does
// nothing until it is on. Returns the option spec to hand to
// user_io_status_get/set, its ex flag, and the value index whose label reads
// "On" (read from the config string rather than assumed to be 1).
bool zaparoo_confstr_autosave(char *opt, int opt_size, int *ex, uint32_t *on_value);

// Finds the core's own explicit save row: the menu entry it labels "Save Backup
// RAM", or "Save Memory Cards" on PSX. In every core that has one the save FSM
// reads bk_save = <this bit> | <autosave path>, so pulsing the bit triggers a
// dump on its own, with no OSD_STATUS edge, no pending-change flag and no
// Autosave option involved.
//
// usable reports whether the core currently shows the row as live, evaluating
// its H/D prefixes against UIO_GET_OSDMASK. Cores use those to say the trigger
// would do nothing: D0 means no save is mounted, Game Boy hides it for a cart
// with no battery, NES hides it when its own Autosave is already on.
bool zaparoo_confstr_save_row(char *opt, int opt_size, int *ex, bool *usable);
