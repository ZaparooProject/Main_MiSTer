#include "../command_stream.h"
#include "../scanout_proxy.h"
#include <assert.h>
#include <string>
#include <vector>
#include <stdio.h>

int main()
{
	ZaparooCommandStream stream;
	std::vector<std::string> lines;
	auto collect = [&](char *line) { lines.push_back(line); };
	const std::string commands = "fb_cmd0 8888 1920 1080\nfb_cmd0 8888 960 540\n";
	stream.feed(commands.data(), commands.size(), collect);
	assert(lines.size() == 2);
	assert(lines[0] == "fb_cmd0 8888 1920 1080");
	assert(lines[1] == "fb_cmd0 8888 960 540");
	lines.clear();
	for (char c : commands) stream.feed(&c, 1, collect);
	assert(lines.size() == 2 && lines[1] == "fb_cmd0 8888 960 540");
	lines.clear();
	std::string oversized(1100, 'x');
	oversized += "\n\nnext\r\n";
	stream.feed(oversized.data(), oversized.size(), collect);
	assert(lines.size() == 1 && lines[0] == "next");
	lines.clear();
	const char invalid[] = {'b', 'a', 'd', 0, 'x', '\n', 'o', 'k', '\n'};
	stream.feed(invalid, sizeof(invalid), collect);
	assert(lines.size() == 1 && lines[0] == "ok");

	using namespace zaparoo_scanout;
	unsigned char packet[64] = {};
	for (uint16_t command : {0x57, 0x59, 0x5B})
	{
		proxy_store(packet, proxy_magic);
		proxy_store(packet + 2, 0xFEDC);
		proxy_store(packet + 4, command);
		proxy_store(packet + 6, proxy_words(command));
		size_t length = 8 + proxy_words(command) * 2;
		assert(proxy_word(packet + 2) == 0xFEDC);
		assert(proxy_valid(packet, length));
		assert(!proxy_valid(packet, length - 1));
		assert(!proxy_valid(packet, length + 1));
		assert(!proxy_valid(packet, 64));
		proxy_store(packet + 6, 0);
		assert(!proxy_valid(packet, length));
	}
	proxy_store(packet + 4, 0x20);
	assert(!proxy_valid(packet, 8));
	proxy_store(packet, 0);
	assert(!proxy_valid(packet, 0));
	puts("PASS: FIFO framing and scanout proxy validation");
}
