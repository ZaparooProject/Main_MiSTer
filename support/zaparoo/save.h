#pragma once

#include <stdbool.h>

// Forced save and pause. Both work by asserting OSD_STATUS to the core: that
// is the only signal that makes a core dump its battery RAM, and the same
// signal cores pause on. In sys/osd.v the status line and the overlay enable
// are the same bit, and the status only rises with OSD_INFO and OSD_MSG clear,
// so Info() cannot trigger a save and a fully invisible save is impossible.
// Both paths therefore paint a banner rather than leave stale menu content up.
void zaparoo_poll(void);

// Called from the .sav sector-write path so a hold can end when the core goes
// quiet instead of after a blind delay.
void zaparoo_save_note_write(void);

// zaparoo_save [hold_ms]. hold_ms forces a fixed hold instead of the
// quiescence rule; it exists to characterise cores on hardware, not for
// normal use.
bool zaparoo_save_request(unsigned hold_ms);

// zaparoo_pause on|off|toggle. Session only: a core load re-execs Main, so a
// pause can never be left behind.
bool zaparoo_pause_active(void);
void zaparoo_pause_set(bool on);

// Save-on-core-exit hook for fpga_load_rbf. True means the caller must abandon
// this load: the target is stashed and re-issued once the flush finishes.
// The load cannot be waited for in place, because the dump only reaches disk
// while user_io_poll keeps servicing sector writes, and fpga_load_rbf is
// itself called from inside user_io_poll.
bool zaparoo_save_defer_core_load(const char *rbf, const char *cfg, const char *xml);
