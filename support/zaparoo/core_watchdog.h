#pragma once

// Requests Core's service ensure for a dead or unrecognized service PID.
// Core owns identity validation and stale-file cleanup under its start gate;
// reused-PID recovery requires that Core-side fix. Main never removes the file.
// Missing PID files are left alone so an explicit service stop stays stopped.
void zaparoo_core_watchdog_poll(void);
