#pragma once

#include <stdio.h>

inline void json_str(FILE *file, const char *value)
{
	putc('"', file);
	for (; *value; value++)
	{
		const unsigned char c = (unsigned char)*value;
		switch (c)
		{
		case '"':  fputs("\\\"", file); break;
		case '\\': fputs("\\\\", file); break;
		case '\b': fputs("\\b", file); break;
		case '\f': fputs("\\f", file); break;
		case '\n': fputs("\\n", file); break;
		case '\r': fputs("\\r", file); break;
		case '\t': fputs("\\t", file); break;
		default:
			if (c < 0x20) fprintf(file, "\\u%04x", (unsigned)c);
			else putc(c, file);
		}
	}
	putc('"', file);
}
