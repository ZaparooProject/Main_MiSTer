#pragma once

// Info() placed left, centered or right on the top row, for RA popups on
// displays that crop the sides. Defined in menu.cpp.
#define INFO_ALIGN_LEFT   0
#define INFO_ALIGN_CENTER 1
#define INFO_ALIGN_RIGHT  2

void InfoAligned(const char *message, int timeout = 2000, int align = INFO_ALIGN_LEFT, int frame = 0);
