#include "../ra_gate.h"
#include <assert.h>

int main()
{
	assert(ra_gate_is_ra_core("RA_SNES", "/media/fat/_RA_Cores/Cores/SNES_20260101.rbf"));
	assert(ra_gate_is_ra_core("RA_SNES", ""));
	assert(ra_gate_is_ra_core("ra_psx", NULL));
	assert(ra_gate_is_ra_core("SNES", "_RA_Cores/Cores/SNES.rbf"));
	assert(!ra_gate_is_ra_core("SNES", "/media/fat/_Console/SNES_20260101.rbf"));
	assert(!ra_gate_is_ra_core("MENU", "/media/fat/menu.rbf"));
	assert(!ra_gate_is_ra_core("ARCADE_RAIDEN", "/media/fat/_Arcade/cores/raiden.rbf"));
	assert(!ra_gate_is_ra_core("RAMPAGE", NULL));
	assert(!ra_gate_is_ra_core(NULL, NULL));

	assert(ra_gate_is_ra_main("/media/fat/MiSTer_RA"));
	assert(ra_gate_is_ra_main("MiSTer_RA"));
	assert(!ra_gate_is_ra_main("/media/fat/MiSTer_RA2"));
	assert(!ra_gate_is_ra_main("/media/fat/degauss/MiSTer_RA_Degauss"));
	assert(!ra_gate_is_ra_main("/media/fat/MiSTer"));
	assert(!ra_gate_is_ra_main("/media/fat/MiSTer_RA/"));
	assert(!ra_gate_is_ra_main(NULL));
	return 0;
}
