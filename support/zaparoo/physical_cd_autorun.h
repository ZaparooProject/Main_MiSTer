#pragma once

// Polls for newly inserted game CDs while the menu core is active and launches
// a matching MGL from an installed physical-CD provider. The persisted OSD
// setting gates all work.
void physical_cd_autorun_poll(void);
