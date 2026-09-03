// Zaparoo launcher input-device classification and metadata export.
//
// This file is #included directly into input.cpp (it is intentionally not a
// .cpp, so the Makefile's support/*/*.cpp glob skips it). It needs input.cpp's
// file-static device tables (input[], pool[], devInput, NUMDEV), which are not
// exposed in any header; #including keeps the logic out of input.cpp while
// avoiding widening the upstream surface to export those internals.

// uinput/virtual devices resolve under /sys/devices/virtual/; live symlink avoids
// relying on input[dev].sysfs, which isn't populated yet at launcher spawn.
static bool zaparoo_is_virtual_dev(int dev)
{
	const char *base = strrchr(input[dev].devname, '/');
	if (base && base[1])
	{
		char link[256], target[512];
		snprintf(link, sizeof(link), "/sys/class/input/%s", base + 1);
		ssize_t n = readlink(link, target, sizeof(target) - 1);
		if (n > 0)
		{
			target[n] = 0;
			return strstr(target, "/virtual/") != NULL;
		}
	}
	return input[dev].sysfs[0] && strstr(input[dev].sysfs, "/virtual/") != NULL;
}

static bool zaparoo_dev_keybits(int dev, unsigned char *kb, size_t sz)
{
	if (pool[dev].fd < 0) return false;
	return ioctl(pool[dev].fd, EVIOCGBIT(EV_KEY, sz), kb) >= 0;
}

#define ZAP_KB_BIT(kb, b) ((kb)[(b) / 8] & (1 << ((b) % 8)))

static bool zaparoo_is_keyboard_dev(int dev)
{
	if (input[dev].mouse || zaparoo_is_virtual_dev(dev)) return false;
	unsigned char kb[(KEY_MAX + 7) / 8] = {};
	if (!zaparoo_dev_keybits(dev, kb, sizeof(kb))) return false;
	return ZAP_KB_BIT(kb, KEY_ESC) && ZAP_KB_BIT(kb, KEY_A) && ZAP_KB_BIT(kb, KEY_Z);
}

static bool zaparoo_is_gamepad_dev(int dev)
{
	if (input[dev].mouse || zaparoo_is_virtual_dev(dev)) return false;
	unsigned char kb[(KEY_MAX + 7) / 8] = {};
	if (!zaparoo_dev_keybits(dev, kb, sizeof(kb))) return false;
	return ZAP_KB_BIT(kb, BTN_GAMEPAD) || ZAP_KB_BIT(kb, BTN_JOYSTICK);
}

#undef ZAP_KB_BIT

static void zaparoo_fill_keyboard(launcher_input_snapshot *s)
{
	s->kbd_present = false;
	for (int dev = 0; dev < NUMDEV; dev++)
	{
		if (zaparoo_is_keyboard_dev(dev))
		{
			s->kbd_present = true;
			strncpy(s->kbd_name, input[dev].name, sizeof(s->kbd_name) - 1);
			s->kbd_vid = input[dev].vid;
			s->kbd_pid = input[dev].pid;
			break;
		}
	}
}

static void zaparoo_fill_launcher_snapshot(launcher_input_snapshot *s, int dev)
{
	memset(s, 0, sizeof(*s));
	strncpy(s->name, input[dev].name, sizeof(s->name) - 1);
	strncpy(s->idstr, input[dev].idstr, sizeof(s->idstr) - 1);
	s->vid = input[dev].vid;
	s->pid = input[dev].pid;
	s->unique_hash = input[dev].unique_hash;
	memcpy(s->mmap, input[dev].mmap, sizeof(s->mmap));
	zaparoo_fill_keyboard(s);
}

// source NULL auto-picks: keyboard when no gamepad present, else controller.
static void zaparoo_export_metadata(const char *source)
{
	int best = -1, best_score = -1;
	for (int dev = 0; dev < NUMDEV; dev++)
	{
		if (!zaparoo_is_gamepad_dev(dev)) continue;
		int score = (input[dev].has_mmap ? 2 : 0) + (input[dev].num == 1 ? 1 : 0);
		if (score > best_score) { best_score = score; best = dev; }
	}

	if (!source) source = (best >= 0) ? "controller" : "keyboard";

	launcher_input_snapshot snap;
	if (best < 0)
	{
		memset(&snap, 0, sizeof(snap));
		zaparoo_fill_keyboard(&snap);
		launcher_input_metadata_write(&snap, 0, source);
		return;
	}

	zaparoo_fill_launcher_snapshot(&snap, best);
	launcher_input_metadata_write(&snap, input[best].num, source);
}

void input_export_launcher_metadata(void)
{
	zaparoo_export_metadata(NULL);
}

// Node to hand gcdb_map_for_controller() when input_cb loads a merged device's
// map. gcdb resolves a mapping's `bN` entries by enumerating the probed node's
// EV_KEY bits (get_ctrl_index_maps), while `hN.x` hat entries are arithmetic.
// Probed through a DualSense Touchpad node, every face button resolves to a
// touchpad code and the d-pad still works: exactly "d-pad moves, buttons dead"
// from beta testing, and the result is cached by vid/pid, so one wrong probe
// poisons every later rescan. Prefer the group's gamepad node.
static int zaparoo_gcdb_probe_dev(int dev, int sub_dev)
{
	if (input[sub_dev].quirk != QUIRK_DS4TOUCH) return sub_dev;
	for (int i = 0; i < NUMDEV; i++)
	{
		if (i != sub_dev && pool[i].fd >= 0 && input[i].bind == dev && input[i].quirk == QUIRK_DS4) return i;
	}
	return sub_dev;
}

// input_cb loads a device's maps on its first event: config/inputs lookups plus
// both gamecontrollerdb files, ~300 ms cold on exFAT. Every /dev/input rescan
// wipes that state, so the first press after boot or a hotplug paid it. Run the
// same loads at open time instead by feeding input_cb a centred, unmapped axis
// event, which is otherwise inert (it stops at the axis_pos compare). Menu core
// only: the boot scan runs before the frontend is spawned, and game cores keep
// stock behaviour.
static void zaparoo_prewarm_device_maps(int n)
{
	if (mapping || !is_menu()) return;
	for (int i = 0; i < n; i++)
	{
		if (pool[i].fd < 0 || input[i].mouse || input[i].force_joy) continue;
		// Only the merge base of a multi-node device (input[].bind, set by
		// mergedevs() before this runs). A DualSense contributes a gamepad and
		// a Touchpad node with the same Phys/Uniq; whichever node reaches
		// input_cb first is the one gcdb probes for the shared map, and the
		// touchpad is the wrong answer (see zaparoo_gcdb_probe_dev).
		if (input[i].bind != i || input[i].quirk == QUIRK_DS4TOUCH) continue;
		struct input_event ev = {};
		ev.type = EV_ABS;
		ev.code = ABS_MISC;
		struct input_absinfo ai = {};
		ai.minimum = -1;
		ai.maximum = 1;
		input_cb(&ev, &ai, i);
	}
}
