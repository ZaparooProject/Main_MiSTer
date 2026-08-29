#pragma once

#include <stdbool.h>

// Dispatcher for every "zaparoo_" command arriving on /dev/MiSTer_cmd:
//
//   zaparoo_console <action> <nonce> [vt]   (handled by alt_launcher)
//   zaparoo_kiosk    on|off|toggle          (persisted)
//   zaparoo_frontend on|off|toggle          (persisted)
//   zaparoo_osd      open|close|toggle      (session-only kiosk bypass)
//   zaparoo_save     [hold_ms]              (force the core to write its save)
//   zaparoo_pause    on|off|toggle
//   zaparoo_mount    <pos> [path]          (disk swap, 1-based, no path ejects)
//
// Returns true when the command was recognised.
bool zaparoo_command(const char *cmd);
