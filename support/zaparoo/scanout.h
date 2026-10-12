#ifndef ZAPAROO_SCANOUT_H
#define ZAPAROO_SCANOUT_H

#include <sys/types.h>

// Private parent/child handshake. v2 grants scanout slots after video setup;
// Main proxies complete UIO packets and remains the sole FPGA-bus writer.
namespace zaparoo_scanout {
// What the child may be granted. A native grant covers the module's mapping of
// the Menu core's native video window: no slots, no proxy, no owned() state.
enum class Offer { none, proxy, native };
void prepare(Offer offer);
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
