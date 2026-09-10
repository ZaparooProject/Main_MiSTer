#ifndef ZAPAROO_SCANOUT_H
#define ZAPAROO_SCANOUT_H

#include <sys/types.h>

// Private parent/child handshake. Merely exporting an offer grants no FPGA
// access; only an acknowledged request after video setup transfers ownership.
namespace zaparoo_scanout {
void prepare(bool eligible);
void child_environment();
void parent_started(pid_t pid);
void stop();
bool poll(bool video_ready);
bool owned();
bool offered();
bool bootstrap_available();
bool blank_framebuffer();
}

#endif
