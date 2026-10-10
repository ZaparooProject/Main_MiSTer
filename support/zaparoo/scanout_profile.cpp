#include "scanout_profile.h"
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

namespace zaparoo_scanout {
namespace {
bool hex(const std::string &value, size_t minimum, size_t maximum)
{
	return value.size() >= minimum && value.size() <= maximum &&
		!(value.size() % 2) && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool read_file(const std::string &path, std::string &out, size_t limit)
{
	out.clear();
	FILE *file = fopen(path.c_str(), "re");
	if (!file) return false;
	char data[256];
	size_t count;
	while ((count = fread(data, 1, sizeof(data), file)))
	{
		out.append(data, count);
		if (out.size() > limit) break;
	}
	bool ok = !ferror(file) && out.size() <= limit;
	fclose(file);
	return ok;
}
bool file_build_id(const char *path, std::string &id)
{
	std::string bytes;
	return read_file(path, bytes, 65536) &&
		note_build_id(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), id);
}
uint32_t little32(const unsigned char *p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

bool note_build_id(const unsigned char *data, size_t size, std::string &id)
{
	id.clear();
	while (size)
	{
		if (size < 12) return false;
		uint32_t names = little32(data), desc = little32(data + 4), type = little32(data + 8);
		data += 12; size -= 12;
		// Bound before rounding so corrupt u32 lengths cannot wrap on ARM32.
		if (names > size || desc > size) return false;
		size_t name_bytes = (size_t(names) + 3) & ~size_t(3);
		size_t desc_bytes = (size_t(desc) + 3) & ~size_t(3);
		if (name_bytes > size || desc_bytes > size - name_bytes) return false;
		if (type == 3 && names == 4 && !memcmp(data, "GNU\0", 4))
		{
			if (!id.empty() || desc < 16 || desc > 64) return false;
			const char digits[] = "0123456789abcdef";
			for (size_t i = 0; i < desc; ++i)
			{
				unsigned char c = data[name_bytes + i];
				id += digits[c >> 4]; id += digits[c & 15];
			}
		}
		data += name_bytes + desc_bytes; size -= name_bytes + desc_bytes;
	}
	return !id.empty();
}

bool parse_profile(const std::string &text, Profile &profile)
{
	std::string lines[7];
	size_t start = 0;
	for (auto &line : lines)
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos) return false;
		line = text.substr(start, end - start); start = end + 1;
	}
	if (start != text.size() || lines[0] != "ZAPAROO-SCANOUT-PROFILE-1" ||
		lines[1].empty() || lines[1].size() > 64 || lines[1] == "." || lines[1] == ".." ||
		lines[1].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos ||
		!hex(lines[2], 32, 128) || !hex(lines[3], 32, 128) ||
		!hex(lines[4], 64, 64) || !hex(lines[5], 40, 40) ||
		lines[6] != "zaparoo-scanout-v2-native") return false;
	profile.release = lines[1]; profile.kernel_id = lines[2]; profile.module_id = lines[3];
	profile.sha256 = lines[4]; profile.revision = lines[5]; profile.contract = lines[6];
	return true;
}

bool select_profile(Profile &profile)
{
	struct utsname kernel;
	std::string id, text;
	if (uname(&kernel) || !file_build_id("/sys/kernel/notes", id)) return false;
	// Validate the release before using it as a path component.
	std::string release(kernel.release);
	if (release.empty() || release == "." || release == ".." ||
		release.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos) return false;
	std::string directory = "/media/fat/zaparoo/modules/" + release + "/" + id;
	if (!read_file(directory + "/profile", text, 1024) || !parse_profile(text, profile) ||
		profile.release != release || profile.kernel_id != id) return false;
	profile.directory = directory;
	return true;
}

bool loaded_profile_matches(const Profile &profile)
{
	std::string id;
	return file_build_id("/sys/module/zaparoo_scanout/notes/.note.gnu.build-id", id) && id == profile.module_id;
}

bool verify_module_fd(int fd, const Profile &profile)
{
	int output[2];
	if (pipe(output)) return false;
	pid_t owner = getpid();
	pid_t pid = fork();
	if (!pid)
	{
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (getppid() != owner) _exit(127);
		close(output[0]);
		if (dup2(output[1], STDOUT_FILENO) < 0) _exit(127);
		close(output[1]);
		char path[64];
		snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
		execlp("sha256sum", "sha256sum", path, static_cast<char *>(NULL));
		_exit(127);
	}
	close(output[1]);
	if (pid < 0) { close(output[0]); return false; }
	char hash[256] = {};
	size_t used = 0;
	while (used < sizeof(hash))
	{
		ssize_t n = read(output[0], hash + used, sizeof(hash) - used);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		used += size_t(n);
	}
	close(output[0]);
	int status = 0;
	pid_t result;
	do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
	return result == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
		used >= 65 && used < sizeof(hash) && hash[64] == ' ' && !memcmp(hash, profile.sha256.data(), 64);
}
}
