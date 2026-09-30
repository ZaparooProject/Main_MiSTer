#include "../scanout_profile.h"
#include "../scanout_conflict.h"
#include <assert.h>
#include <string>
#include <vector>

int main()
{
	using namespace zaparoo_scanout;
	assert(internal_mapping_process(10, 10, 20, 30));
	assert(internal_mapping_process(20, 10, 20, 30));
	assert(internal_mapping_process(30, 10, 20, 30));
	assert(!internal_mapping_process(40, 10, 20, 30));
	assert(!internal_mapping_process(0, 10, 0, 0));
	assert(!internal_mapping_process(-1, 10, 20, -1));
	assert(!internal_mapping_process(30, 10, 20, 0));
	Profile profile;
	const std::string release = "6.18.38-MiSTer";
	const std::string input = "ZAPAROO-SCANOUT-PROFILE-1\n" + release + "\n" +
		std::string(40, 'a') + "\n" + std::string(40, 'b') + "\n" +
		std::string(64, 'c') + "\n" + std::string(40, 'd') + "\nzaparoo-scanout-v1-1080p\n";
	assert(parse_profile(input, profile));
	assert(profile.release == release && profile.kernel_id == std::string(40, 'a'));
	assert(!parse_profile(input + "extra\n", profile));
	assert(!parse_profile(input.substr(0, input.size() - 1), profile));
	std::string invalid = input;
	invalid.replace(invalid.find(release), release.size(), "../escape");
	assert(!parse_profile(invalid, profile));
	invalid = input;
	invalid[invalid.find(std::string(40, 'a'))] = 'G';
	assert(!parse_profile(invalid, profile));
	// GNU note followed by a Linux note, as in /sys/kernel/notes.
	std::vector<unsigned char> note = {4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
	note.insert(note.end(), 20, 0xab);
	std::string id;
	assert(note_build_id(note.data(), note.size(), id));
	assert(id == "abababababababababababababababababababab");
	auto duplicate = note;
	duplicate.insert(duplicate.end(), note.begin(), note.end());
	assert(!note_build_id(duplicate.data(), duplicate.size(), id));
	for (size_t size = 0; size < note.size(); ++size)
		assert(!note_build_id(note.data(), size, id));
	note[0] = 255; note[1] = 255; note[2] = 255; note[3] = 255;
	assert(!note_build_id(note.data(), note.size(), id));
	return 0;
}
