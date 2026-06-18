// Writes /tmp/zaparoo_launcher_input.json describing the active launcher controller
// and resolved action bindings. See launcher_input_metadata_plan.md.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input.h>

#include "launcher_input_metadata.h"
#include "../../input.h"

#define LIM_PATH      "/tmp/zaparoo_launcher_input.json"
#define LIM_PATH_TMP  "/tmp/zaparoo_launcher_input.json.tmp"

struct slot_position
{
	int         slot;
	const char *sys_button;
	const char *position;
};

// SYS_BTN_* slots hold SDL-swapped physical buttons (gamecontroller_db.cpp:14):
// A=east, B=south, X=north, Y=west.
static const slot_position POSITIONS[] =
{
	{ SYS_BTN_A,      "SYS_BTN_A",      "east"           },
	{ SYS_BTN_B,      "SYS_BTN_B",      "south"          },
	{ SYS_BTN_X,      "SYS_BTN_X",      "north"          },
	{ SYS_BTN_Y,      "SYS_BTN_Y",      "west"           },
	{ SYS_BTN_L,      "SYS_BTN_L",      "left_shoulder"  },
	{ SYS_BTN_R,      "SYS_BTN_R",      "right_shoulder" },
	{ SYS_BTN_START,  "SYS_BTN_START",  "start"          },
	{ SYS_BTN_SELECT, "SYS_BTN_SELECT", "select"         },
};

enum { PROFILE_XBOX = 0, PROFILE_NINTENDO, PROFILE_PLAYSTATION, PROFILE_GENERIC };

struct label_row
{
	const char *position;
	const char *label[4]; // indexed by PROFILE_*
};

static const label_row LABELS[] =
{
	{ "south",          { "A",    "B",    "Cross",    "A"      } },
	{ "east",           { "B",    "A",    "Circle",   "B"      } },
	{ "west",           { "X",    "Y",    "Square",   "Y"      } },
	{ "north",          { "Y",    "X",    "Triangle", "X"      } },
	{ "left_shoulder",  { "LB",   "L",    "L1",       "L"      } },
	{ "right_shoulder", { "RB",   "R",    "R1",       "R"      } },
	{ "start",          { "Menu", "+",    "Options",  "Start"  } },
	{ "select",         { "View", "-",    "Share",    "Select" } },
	{ "guide",          { "Guide","Home", "PS",       "Menu"   } },
};

static const slot_position *slot_info(int slot)
{
	for (size_t i = 0; i < sizeof(POSITIONS) / sizeof(POSITIONS[0]); i++)
	{
		if (POSITIONS[i].slot == slot) return &POSITIONS[i];
	}
	return NULL;
}

static const char *label_for(const char *position, int profile)
{
	for (size_t i = 0; i < sizeof(LABELS) / sizeof(LABELS[0]); i++)
	{
		if (!strcmp(LABELS[i].position, position)) return LABELS[i].label[profile];
	}
	return "";
}

static const slot_position *reverse_map(const launcher_input_snapshot *snap, uint16_t code, int default_slot)
{
	if (code)
	{
		for (size_t i = 0; i < sizeof(POSITIONS) / sizeof(POSITIONS[0]); i++)
		{
			if ((uint16_t)snap->mmap[POSITIONS[i].slot] == code) return &POSITIONS[i];
		}
	}
	return slot_info(default_slot);
}

static bool name_has(const char *name, const char *needle)
{
	char lname[sizeof(((launcher_input_snapshot *)0)->name)];
	size_t i = 0;
	for (; name[i] && i < sizeof(lname) - 1; i++)
	{
		char c = name[i];
		lname[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
	}
	lname[i] = 0;
	return strstr(lname, needle) != NULL;
}

static const char *PROFILE_NAMES[4] = { "xbox", "nintendo", "playstation", "generic" };

static int detect_profile(const launcher_input_snapshot *snap, const char **confidence)
{
	switch (snap->vid)
	{
	case 0x054C: *confidence = "detected"; return PROFILE_PLAYSTATION; // Sony
	case 0x057E: *confidence = "detected"; return PROFILE_NINTENDO;    // Nintendo
	case 0x045E: *confidence = "detected"; return PROFILE_XBOX;        // Microsoft
	}

	const char *n = snap->name;
	if (name_has(n, "dualshock") || name_has(n, "dualsense") || name_has(n, "playstation") ||
		name_has(n, "sony") || name_has(n, "ps4") || name_has(n, "ps5"))
	{
		*confidence = "name_match";
		return PROFILE_PLAYSTATION;
	}
	if (name_has(n, "nintendo") || name_has(n, "switch") || name_has(n, "joy-con") ||
		name_has(n, "joycon") || name_has(n, "pro controller") || name_has(n, "8bitdo"))
	{
		*confidence = "name_match";
		return PROFILE_NINTENDO;
	}
	if (name_has(n, "xbox") || name_has(n, "x-box") || name_has(n, "microsoft") || name_has(n, "xinput"))
	{
		*confidence = "name_match";
		return PROFILE_XBOX;
	}

	*confidence = "generic";
	return PROFILE_GENERIC;
}

static void json_str(FILE *fp, const char *s)
{
	putc('"', fp);
	for (; *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		switch (c)
		{
		case '"':  fputs("\\\"", fp); break;
		case '\\': fputs("\\\\", fp); break;
		case '\b': fputs("\\b", fp);  break;
		case '\f': fputs("\\f", fp);  break;
		case '\n': fputs("\\n", fp);  break;
		case '\r': fputs("\\r", fp);  break;
		case '\t': fputs("\\t", fp);  break;
		default:
			if (c < 0x20) fprintf(fp, "\\u%04x", c);
			else putc(c, fp);
		}
	}
	putc('"', fp);
}

struct action_out
{
	const char *name;
	const char *key;
	int         key_code;
	const char *virt;
	const char *sys_button;
	int         code;
	const char *position;
	const char *label;
	const char *source;
};

static void emit_action(FILE *fp, const action_out *a, bool last)
{
	fputs("    ", fp);
	json_str(fp, a->name);
	fputs(": {\n", fp);
	fputs("      \"key\": ", fp);          json_str(fp, a->key);        fputs(",\n", fp);
	fprintf(fp, "      \"key_code\": %d,\n", a->key_code);
	fputs("      \"virtual\": ", fp);      json_str(fp, a->virt);       fputs(",\n", fp);
	fputs("      \"sys_button\": ", fp);   json_str(fp, a->sys_button); fputs(",\n", fp);
	fprintf(fp, "      \"code\": %d,\n", a->code);
	fputs("      \"position\": ", fp);     json_str(fp, a->position);   fputs(",\n", fp);
	fputs("      \"label\": ", fp);        json_str(fp, a->label);      fputs(",\n", fp);
	fputs("      \"source\": ", fp);       json_str(fp, a->source);     fputs("\n", fp);
	fputs(last ? "    }\n" : "    },\n", fp);
}

void launcher_input_metadata_write(const launcher_input_snapshot *snap, int player, const char *active_source)
{
	if (!snap) return;
	if (!active_source) active_source = "controller";

	const char *confidence = "generic";
	int profile = detect_profile(snap, &confidence);

	uint32_t mf = snap->mmap[SYS_BTN_MENU_FUNC];
	uint16_t ok_code   = (mf & 0xFFFF) ? (uint16_t)(mf & 0xFFFF) : (uint16_t)snap->mmap[SYS_BTN_A];
	uint16_t back_code = (mf >> 16)    ? (uint16_t)(mf >> 16)    : (uint16_t)snap->mmap[SYS_BTN_B];
	const char *ok_source   = (mf & 0xFFFF) ? "menuok"  : "mmap";
	const char *back_source = (mf >> 16)    ? "menuesc" : "mmap";

	const slot_position *ok_slot   = reverse_map(snap, ok_code, SYS_BTN_A);
	const slot_position *back_slot = reverse_map(snap, back_code, SYS_BTN_B);

	const slot_position *y_slot     = slot_info(SYS_BTN_Y);
	const slot_position *x_slot     = slot_info(SYS_BTN_X);
	const slot_position *l_slot     = slot_info(SYS_BTN_L);
	const slot_position *r_slot     = slot_info(SYS_BTN_R);
	const slot_position *start_slot = slot_info(SYS_BTN_START);
	const slot_position *sel_slot   = slot_info(SYS_BTN_SELECT);

	action_out actions[] =
	{
		{ "ok",        "Enter",    KEY_ENTER,    "A",   ok_slot->sys_button,   ok_code,                              ok_slot->position,   label_for(ok_slot->position, profile),   ok_source   },
		{ "back",      "Esc",      KEY_ESC,      "B",   back_slot->sys_button, back_code,                            back_slot->position, label_for(back_slot->position, profile), back_source },
		{ "info",      "Space",    KEY_SPACE,    "Y",   y_slot->sys_button,    (int)(uint16_t)snap->mmap[SYS_BTN_Y], y_slot->position,    label_for(y_slot->position, profile),    "mmap"      },
		{ "options",   "Tab",      KEY_TAB,      "X",   x_slot->sys_button,    (int)(uint16_t)snap->mmap[SYS_BTN_X], x_slot->position,    label_for(x_slot->position, profile),    "mmap"      },
		{ "page_prev", "PageUp",   KEY_PAGEUP,   "L",   l_slot->sys_button,    (int)(uint16_t)snap->mmap[SYS_BTN_L], l_slot->position,    label_for(l_slot->position, profile),    "mmap"      },
		{ "page_next", "PageDown", KEY_PAGEDOWN, "R",   r_slot->sys_button,    (int)(uint16_t)snap->mmap[SYS_BTN_R], r_slot->position,    label_for(r_slot->position, profile),    "mmap"      },
		{ "help",      "F1",       KEY_F1,       "L2",  start_slot->sys_button,(int)(uint16_t)snap->mmap[SYS_BTN_START], start_slot->position, label_for(start_slot->position, profile), "mmap"  },
		{ "quit",      "Backspace",KEY_BACKSPACE,"R2",  sel_slot->sys_button,  (int)(uint16_t)snap->mmap[SYS_BTN_SELECT], sel_slot->position,   label_for(sel_slot->position, profile),   "mmap"  },
		{ "menu",      "Menu",     KEY_MENU,     "OSD", "BTN_OSD",             0,                                    "guide",             label_for("guide", profile),             "osd"       },
	};

	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t updated_ms = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);

	FILE *fp = fopen(LIM_PATH_TMP, "wt");
	if (!fp) return;

	fputs("{\n", fp);
	fprintf(fp, "  \"version\": 1,\n");
	fprintf(fp, "  \"updated_ms\": %llu,\n", (unsigned long long)updated_ms);
	fprintf(fp, "  \"active_player\": %d,\n", player);
	fputs("  \"active_source\": ", fp); json_str(fp, active_source); fputs(",\n", fp);

	fputs("  \"controller\": {\n", fp);
	fputs("    \"name\": ", fp);  json_str(fp, snap->name);  fputs(",\n", fp);
	fputs("    \"idstr\": ", fp); json_str(fp, snap->idstr); fputs(",\n", fp);
	fprintf(fp, "    \"vid\": \"%04x\",\n", snap->vid);
	fprintf(fp, "    \"pid\": \"%04x\",\n", snap->pid);
	fprintf(fp, "    \"unique_hash\": \"%08x\",\n", snap->unique_hash);
	fputs("    \"glyph_profile\": ", fp);      json_str(fp, PROFILE_NAMES[profile]); fputs(",\n", fp);
	fputs("    \"profile_confidence\": ", fp); json_str(fp, confidence);             fputs("\n", fp);
	fputs("  },\n", fp);

	fputs("  \"keyboard\": {\n", fp);
	fprintf(fp, "    \"connected\": %s,\n", snap->kbd_present ? "true" : "false");
	fputs("    \"name\": ", fp); json_str(fp, snap->kbd_present ? snap->kbd_name : ""); fputs(",\n", fp);
	fprintf(fp, "    \"vid\": \"%04x\",\n", snap->kbd_present ? snap->kbd_vid : 0);
	fprintf(fp, "    \"pid\": \"%04x\"\n", snap->kbd_present ? snap->kbd_pid : 0);
	fputs("  },\n", fp);

	fputs("  \"actions\": {\n", fp);
	size_t n = sizeof(actions) / sizeof(actions[0]);
	for (size_t i = 0; i < n; i++) emit_action(fp, &actions[i], i == n - 1);
	fputs("  }\n", fp);
	fputs("}\n", fp);

	fflush(fp);
	fsync(fileno(fp));
	fclose(fp);

	rename(LIM_PATH_TMP, LIM_PATH);
}
