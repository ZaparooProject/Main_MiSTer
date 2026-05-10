#pragma once

#include <stdbool.h>
#include <stdint.h>

// Right-side companion to the trimmed System Settings menu, reachable
// via the right arrow when alt_launcher_configured() is true. This file
// hosts both pages of that companion: the top-level "Zaparoo Launcher"
// page and the nested "Video" sub-page that owns the CRT mode toggle
// plus the H/V centering offsets.
//
// The Video page binds left/right arrows to value adjustment instead of
// sibling navigation, which is why it lives behind a sub-page rather
// than directly under System Settings.

// Top "Zaparoo Launcher" page. menusub layout: 0 = Video, 1 = Exit.
void launcher_page_render(int menusub, uint64_t *menumask);

// Translates a select press on the Launcher page.
//   1 -> entered Video sub-page
//   0 -> Exit pressed (close OSD)
//  -1 -> no-op
int launcher_page_handle_select(int menusub);

// Video sub-page. menusub layout: 0 = CRT mode, 1 = H Offset,
// 2 = V Offset, 3 = Exit.
//
// The OSD is closed automatically by the launcher spawn path (see
// MenuHide() in spawn() in alt_launcher.cpp) when CRT toggling
// triggers a respawn — these helpers don't need to signal that.
void video_page_render(int menusub, uint64_t *menumask);

// Returns true if the press was consumed (re-render only); false when
// Exit was selected (caller pops back to the Launcher page).
bool video_page_handle_select(int menusub);

// Adjust the highlighted row by `dir` (-1 / +1). Toggles CRT mode on
// the CRT row regardless of sign; ±1 on the H/V offset rows; no-op
// elsewhere.
void video_page_adjust(int menusub, int dir);
