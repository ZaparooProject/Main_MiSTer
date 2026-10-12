#ifndef ZAPAROO_SCANOUT_PROXY_H
#define ZAPAROO_SCANOUT_PROXY_H

#include <stddef.h>
#include <stdint.h>

namespace zaparoo_scanout {
// Private protocol v2: little-endian magic, sequence, UIO command, word count,
// followed by exactly that many words. One packet is one complete transaction.
constexpr uint16_t proxy_magic = 0x5A52;
constexpr size_t proxy_max_bytes = 32;
// Answered by Main, never written to the bus: the scaler raster a SET
// destination is expressed in, as width then height. Outside the 8-bit UIO
// command space. Offered to the child as ZAPAROO_SCANOUT_RASTER=1.
constexpr uint16_t proxy_raster = 0x0100;
inline uint16_t proxy_word(const unsigned char *p)
{
	return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline void proxy_store(unsigned char *p, uint16_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
}
constexpr unsigned proxy_words(uint16_t command)
{
	return command == 0x57 ? 12 : command == 0x59 ? 6 : command == 0x5B ? 11 :
	       command == proxy_raster ? 2 : 0;
}
inline bool proxy_valid(const unsigned char *p, size_t size)
{
	if (size < 8 || size > proxy_max_bytes || proxy_word(p) != proxy_magic) return false;
	unsigned words = proxy_words(proxy_word(p + 4));
	return words && proxy_word(p + 6) == words && size == 8 + words * 2;
}
}
#endif
