#pragma once

// Restarts Core when its PID file remains after an ungraceful process death.
// Missing PID files are left alone so an explicit service stop stays stopped.
void zaparoo_core_watchdog_poll(void);
