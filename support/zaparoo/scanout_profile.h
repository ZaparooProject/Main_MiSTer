#ifndef ZAPAROO_SCANOUT_PROFILE_H
#define ZAPAROO_SCANOUT_PROFILE_H

#include <stddef.h>
#include <string>

namespace zaparoo_scanout {
// Kept separate from FPGA I/O so malformed identities can be tested off-device.
struct Profile {
	std::string release, kernel_id, module_id, sha256, revision, contract;
	std::string directory;
};
bool note_build_id(const unsigned char *data, size_t size, std::string &id);
bool parse_profile(const std::string &text, Profile &profile);
bool select_profile(Profile &profile);
bool loaded_profile_matches(const Profile &profile);
// Called only in the asynchronous loader child; never blocks Main's poll loop.
bool verify_module_fd(int fd, const Profile &profile);
}
#endif
