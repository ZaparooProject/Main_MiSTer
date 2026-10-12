// Zaparoo launcher: releases a direction whose axis has gone back to rest
// without Main seeing it.
//
// input_test() turns every axis into direction presses and remembers one edge
// per axis in input[dev].axis_edge. Only an event from that axis clears it, so
// a missed return to center leaves the direction held: the launcher sees a key
// that never comes up and keeps repeating it. An event can go missing when the
// kernel's queue for the device overflows (SYN_DROPPED, which input_test()
// does not act on), and the launcher path leaves the read loop early, which
// makes a backlog likelier than upstream's.
//
// So a held edge is checked against the axis value the kernel holds now. If
// the kernel says the axis is at rest, the direction is released. If it says
// the axis is still deflected, the hold came from the device or its driver and
// only the log records it. A press is never made up.
//
// #included into input.cpp like launcher_input_detect.inc, for the same
// reason: it needs that file's static device tables.

// Hats and sticks all sit below ABS_MISC; nothing above it is a direction.
#define ZAP_AXIS_CODES 0x28
#define ZAP_AXIS_FIRST_CHECK_MS 200
#define ZAP_AXIS_RECHECK_MS 1000
#define ZAP_AXIS_SCAN_MS 50

struct zaparoo_axis_hold
{
	uint8_t state;
	uint8_t checks;
	unsigned long check;
};

static zaparoo_axis_hold s_zaparoo_axis_hold[NUMDEV][ZAP_AXIS_CODES];
static bool s_zaparoo_axis_dropped[NUMDEV];
static unsigned long s_zaparoo_axis_scan = 0;

static bool zaparoo_axis_is_hat(const struct input_absinfo *abs)
{
	return (abs->maximum == 1 && abs->minimum == -1) || (abs->maximum == 2 && abs->minimum == 0);
}

// cfg.dpad_threshold is newer than the upstream release the stable build is
// based on. Read it where it exists; otherwise use the fixed 50% that
// release hardcodes, so this file builds on both bases.
template <typename T> static auto zaparoo_dpad_threshold(const T &c, int) -> decltype(int(c.dpad_threshold)) { return c.dpad_threshold; }
template <typename T> static int zaparoo_dpad_threshold(const T &, long) { return 50; }

// At rest by a wide margin: a hat off both ends, a stick inside half of the
// threshold input_test() presses a direction at. Anything nearer an edge is
// left to the device's own events.
static bool zaparoo_axis_at_rest(const struct input_absinfo *abs)
{
	if (zaparoo_axis_is_hat(abs)) return abs->value != abs->minimum && abs->value != abs->maximum;
	int range = abs->maximum - abs->minimum + 1;
	int center = abs->minimum + (range / 2);
	int margin = (range * zaparoo_dpad_threshold(cfg, 0)) / 400;
	return abs->value >= center - margin && abs->value <= center + margin;
}

// Every event read from a device passes through here, EV_SYN included.
static void zaparoo_axis_note_event(int i, const struct input_event *ev)
{
	if (ev->type != EV_SYN || ev->code != SYN_DROPPED || !alt_launcher_active()) return;
	if (!s_zaparoo_axis_dropped[i]) printf("zaparoo_input: %s dropped events; rechecking held directions\n", input[i].devname);
	s_zaparoo_axis_dropped[i] = true;
}

static void zaparoo_axis_resync()
{
	if (!alt_launcher_active()) return;

	bool dropped = false;
	for (int i = 0; i < NUMDEV; i++) dropped |= s_zaparoo_axis_dropped[i];
	if (!dropped && s_zaparoo_axis_scan && !CheckTimer(s_zaparoo_axis_scan)) return;
	s_zaparoo_axis_scan = GetTimer(ZAP_AXIS_SCAN_MS);

	for (int i = 0; i < NUMDEV; i++)
	{
		bool recheck = s_zaparoo_axis_dropped[i];
		s_zaparoo_axis_dropped[i] = false;

		// input_test() rewrites the ranges of quirk devices before it derives
		// an edge; the raw kernel value says nothing about those.
		if (pool[i].fd < 0 || input[i].mouse || input[i].quirk) continue;
		int dev = i;
		if (input[dev].bind >= 0) dev = input[dev].bind;
		if (input[dev].lightgun) continue;

		for (int code = 0; code < ZAP_AXIS_CODES; code++)
		{
			zaparoo_axis_hold *hold = &s_zaparoo_axis_hold[i][code];
			uint8_t state = input[dev].axis_edge[code];
			if (!state)
			{
				hold->state = 0;
				continue;
			}
			if (hold->state != state)
			{
				hold->state = state;
				hold->checks = 0;
				hold->check = GetTimer(ZAP_AXIS_FIRST_CHECK_MS);
			}
			if (!recheck && !CheckTimer(hold->check)) continue;
			hold->check = GetTimer(ZAP_AXIS_RECHECK_MS);

			// Bound nodes share one edge table; only the node that has
			// this axis can answer for it.
			struct input_absinfo abs;
			if (ioctl(pool[i].fd, EVIOCGABS(code), &abs) < 0 || abs.maximum == abs.minimum) continue;

			if (!zaparoo_axis_at_rest(&abs))
			{
				// Past the first recheck this is a hold of over a second.
				// One line per hold tells a stuck device from a held one.
				if (hold->checks == 1)
				{
					printf("zaparoo_input: %s axis %d held at edge %d; kernel value %d [%d..%d]\n",
						input[i].devname, code, state, abs.value, abs.minimum, abs.maximum);
				}
				if (hold->checks < 2) hold->checks++;
				continue;
			}

			printf("zaparoo_input: %s axis %d released: Main held edge %d, kernel value %d [%d..%d]\n",
				input[i].devname, code, state, abs.value, abs.minimum, abs.maximum);
			input[dev].axis_edge[code] = 0;
			hold->state = 0;

			struct input_event ev = {};
			ev.type = EV_KEY;
			ev.value = 0;
			ev.code = KEY_EMU + (code << 1) - 1 + state;
			input_cb(&ev, zaparoo_axis_is_hat(&abs) ? 0 : &abs, i);
		}
	}
}
