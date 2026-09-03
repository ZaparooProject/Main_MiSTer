#include "mount.h"
#include "confstr.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// cheats.h and game_docs.h use uint32_t without including stdint themselves.
#include "cheats.h"
#include "file_io.h"
#include "game_docs.h"
#include "menu.h"
#include "user_io.h"
#include "support.h"

// Lists what this core actually offers, so a wrong position is self-explaining
// instead of a trial-and-error hunt over SSH.
static void log_slots(void)
{
	int n = zaparoo_confstr_slot_count();
	if (!n)
	{
		printf("zaparoo_mount: this core has no mountable slots\n");
		return;
	}
	printf("zaparoo_mount: this core offers %d slot%s:\n", n, (n == 1) ? "" : "s");
	for (int i = 1; i <= n; i++)
	{
		char ext[64] = {};
		char label[64] = {};
		int core_slot = 0;
		if (zaparoo_confstr_slot(i, &core_slot, ext, sizeof(ext), label, sizeof(label)))
			printf("  %d  %-12s %s\n", i, ext, label);
	}
}

bool zaparoo_mount(int pos, const char *path)
{
	if (is_menu())
	{
		printf("zaparoo_mount: no core loaded\n");
		return false;
	}

	// pos is the slot's 1-based position in the core's own declaration order,
	// not the core's internal slot number, so the same command works on any
	// core. Resolve it to the real number here.
	int slot = 0;
	char ext[64] = {};
	char label[64] = {};
	if (!zaparoo_confstr_slot(pos, &slot, ext, sizeof(ext), label, sizeof(label)))
	{
		printf("zaparoo_mount: no slot %d on this core\n", pos);
		log_slots();
		return false;
	}

	// UIO_SET_SDSTAT packs the slot as (1 << slot) into an 8-bit word whose
	// bit 7 is the write-protect flag, so anything above 6 corrupts it.
	if (slot < 0 || slot > 6)
	{
		printf("zaparoo_mount: slot %d (position %d) out of range 0-6\n", slot, pos);
		return false;
	}

	// Setters below take non-const pointers, and an eject is just an empty one.
	char p[1024] = {};
	if (path && *path)
	{
		snprintf(p, sizeof(p), "%s", path);
		if (!FileExists(p, 0))
		{
			printf("zaparoo_mount: not found: %s\n", p);
			return false;
		}
	}

	if (!p[0])
	{
		// user_io_file_mount's len == 0 branch closes the image and sends a
		// zero-size UIO_SET_SDSTAT, which is exactly the OSD's eject.
		printf("zaparoo_mount: eject %d (%s, core slot %d)\n", pos, label, slot);
		user_io_file_mount(p, slot);
		return true;
	}

	printf("zaparoo_mount: %d (%s, core slot %d) <- %s\n", pos, label, slot, p);
	StoreIdx_S(slot, p);

	const unsigned char extidx = user_io_ext_idx(p, ext);

	// Dispatch mirrors the SC auto-mount path in user_io_init, not the OSD's,
	// because the OSD version indexes off the highlighted menu row.
	if (is_x86() || is_pcxt() || (is_uneon() && slot >= 2))
	{
		x86_set_image(slot, p);
	}
	else if (is_megacd())
	{
		mcd_set_image(slot, p);
		game_docs_init(p, 0);
	}
	else if (is_pce())
	{
		pcecd_set_image(slot, p);
		game_docs_init(p, 0);
		cheats_init(p, 0);
	}
	else if (is_psx() && slot == 1)
	{
		psx_mount_cd(extidx << 6 | slot, slot, p);
		game_docs_init(p, 0);
		cheats_init(p, 0);
	}
	else if (is_cdi())
	{
		cdi_mount_cd(slot, p);
	}
	else if (is_saturn())
	{
		if (!slot) saturn_set_image(slot, p);
		else saturn_mount_save(p);
	}
	else if (is_neogeo())
	{
		neocd_set_en(1);
		neocd_set_image(p);
	}
	else if (is_atari800())
	{
		atari800_set_image(extidx, slot, p);
	}
	else if (is_3do())
	{
		p3do_set_image(slot, p);
	}
	else
	{
		// user_io_file_mount sets the index itself for C128; setting it here
		// first would fight that.
		if (!is_c128()) user_io_set_index((extidx << 6) | slot);
		user_io_file_mount(p, slot);
	}

	return true;
}
