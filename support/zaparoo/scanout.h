#ifndef ZAPAROO_SCANOUT_H
#define ZAPAROO_SCANOUT_H

#include <sys/types.h>

// Private parent/child handshake. v2 grants scanout slots after video setup;
// Main proxies complete UIO packets and remains the sole FPGA-bus writer.
namespace zaparoo_scanout {
void prepare(bool eligible);
void child_environment();
void parent_started(pid_t pid);
void stop();
bool poll(bool video_ready);
bool owned();
bool bus_owned();
bool offered();
bool bootstrap_available();
bool blank_framebuffer();
}

#endif
