#include "loop_throttle.h"
#include <unistd.h>
#include "alt_launcher.h"
#include "user_io.h"

#define ZAPAROO_THROTTLE_US 2000

void zaparoo_loop_throttle(void)
{
	if (alt_launcher_active() && is_menu()) usleep(ZAPAROO_THROTTLE_US);
}
