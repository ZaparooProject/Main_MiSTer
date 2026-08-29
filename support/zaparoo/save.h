#pragma once

#include <stdbool.h>

// Drives the forced-save state machine, one step per scheduler tick.
//
// Main never initiates a save itself; the core's own Verilog does it. Two
// triggers exist, and every core reads them as bk_save = <explicit bit> |
// <autosave path>. The explicit bit is the core's own "Save Backup RAM" row and
// needs no OSD at all, which is the path this prefers. The fallback raises
// OSD_STATUS, and per sys/osd.v that status only rises with OSD_INFO and
// OSD_MSG clear, so Info() cannot trigger a save; the status line and the
// overlay enable are also the same bit, so that path is necessarily visible and
// paints a banner rather than leaving stale menu content on screen.
void zaparoo_poll(void);

// Called from the sector-write path, on any slot, so a hold can end when the
// core goes quiet instead of after a blind delay. Not restricted to slot 0:
// PSX mounts its memory card higher up and would otherwise never be seen.
void zaparoo_save_note_write(void);

// zaparoo_save. Asks for a flush of the running core's save.
bool zaparoo_save_request(void);

// Save-on-core-exit hook for fpga_load_rbf. True means the caller must abandon
// this load: the target is stashed and re-issued once the flush finishes. The
// load cannot be waited for in place, because the dump only reaches disk while
// user_io_poll services sector writes, and fpga_load_rbf is itself called from
// inside user_io_poll.
bool zaparoo_save_defer_core_load(const char *rbf, const char *cfg, const char *xml);
