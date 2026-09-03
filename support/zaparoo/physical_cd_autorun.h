#pragma once

// Polls for newly inserted physical discs while the menu core is active.
// Game CDs launch through an installed provider MGL; DVDs launch a compatible
// core directly. The persisted OSD setting gates all work.
void physical_cd_autorun_poll(void);
