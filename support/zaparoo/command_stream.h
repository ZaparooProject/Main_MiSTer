#ifndef ZAPAROO_COMMAND_STREAM_H
#define ZAPAROO_COMMAND_STREAM_H
#include <stddef.h>

// FIFO reads need not end at command boundaries. Keep incomplete lines and
// discard an overlong line in full, never execute a truncated path/command.
class ZaparooCommandStream {
	char line[1024] = {};
	size_t used = 0;
	bool overflow = false;
public:
	template<class Dispatch> void feed(const char *bytes, size_t length, Dispatch dispatch)
	{
		for (size_t i = 0; i < length; ++i)
		{
			char c = bytes[i];
			if (c == '\n')
			{
				if (!overflow && used)
				{
					if (line[used - 1] == '\r') --used;
					line[used] = 0;
					if (used) dispatch(line);
				}
				used = 0;
				overflow = false;
			}
			else if (!overflow)
			{
				if (!c || used == sizeof(line) - 1) overflow = true;
				else line[used++] = c;
			}
		}
	}
};
#endif
