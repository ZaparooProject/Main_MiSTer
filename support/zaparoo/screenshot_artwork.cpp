#include "screenshot_artwork.h"

#include <atomic>
#include <memory>
#include <new>
#include <string>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "active_game.h"
#include "hardware.h"
#include "input.h"
#include "json_string.h"
#include "menu.h"
#include "offload.h"
#include "osd.h"
#include "scaler.h"
#include "user_io.h"

namespace {

constexpr unsigned long hold_ms = 750;
constexpr unsigned long capture_timeout_ms = 1000;
#define ARTWORK_INBOX "/media/fat/zaparoo/artwork-inbox"

// Core's canonical media.image type tokens.
constexpr struct ArtworkType {
	const char *label;
	const char *token;
} artwork_types[] = {
	{ "Screenshot", "screenshot" },
	{ "Title screen", "titleshot" },
	{ "Image", "image" },
	{ "Thumbnail", "thumbnail" },
	{ "Box art", "boxart" },
	{ "3D box art", "boxart3d" },
	{ "Box side", "boxartside" },
	{ "Box back", "boxartback" },
	{ "Logo", "wheel" },
	{ "Map", "map" },
	{ "Marquee", "marquee" },
	{ "Fan art", "fanart" },
};
constexpr int type_count = sizeof(artwork_types) / sizeof(artwork_types[0]);
static_assert(type_count <= 14, "The artwork choices must fit above Cancel");
constexpr const char *scope_labels[] = { "This version only", "All versions of this title", "Back" };
constexpr const char *scope_tokens[] = { "version", "title" };

enum class CaptureState { idle, requested, ready, no_game, failed };
enum class SaveState { writing, saved, failed };
enum class Page { types, scope, save };

struct Capture
{
	std::string path;
	std::string core;
	int width = 0;
	int height = 0;
	std::unique_ptr<uint8_t[]> pixels;
	std::atomic<SaveState> save{SaveState::writing};
};

unsigned long hold_deadlines[NUMPLAYERS] = {};
unsigned long capture_deadline;
CaptureState capture_state = CaptureState::idle;
std::shared_ptr<Capture> capture;
Page page = Page::types;
int selection;
int selected_type;
SaveState displayed_save;

bool write_manifest(const std::string &path, const Capture &frame, const char *type, const char *scope)
{
	FILE *file = fopen(path.c_str(), "w");
	if (!file) return false;
	fputs("{\"version\":1,\"gamePath\":", file);
	json_str(file, frame.path.c_str());
	fputs(",\"corePath\":", file);
	json_str(file, frame.core.c_str());
	fprintf(file, ",\"imageType\":\"%s\",\"scope\":\"%s\"}\n", type, scope);
	const bool written = !ferror(file);
	return fclose(file) == 0 && written;
}

bool stage_capture(const Capture &frame, const char *type, const char *scope)
{
	if (mkdir(ARTWORK_INBOX, 0755) && errno != EEXIST)
	{
		perror("Screenshot artwork: create inbox");
		return false;
	}

	// Publish a completed pair with one rename. Core can consume capture-*
	// directories. A hidden directory is still being written or was interrupted.
	char temp[] = ARTWORK_INBOX "/.capture-XXXXXX";
	if (!mkdtemp(temp))
	{
		perror("Screenshot artwork: create staging directory");
		return false;
	}
	const std::string png = std::string(temp) + "/capture.png";
	const std::string manifest = std::string(temp) + "/request.json";
	std::string ready = temp;
	ready.erase(ready.find_last_of('/') + 1, 1); // Remove the dot from the staging name.
	if (write_screenshot(png.c_str(), frame.pixels.get(), frame.width, frame.height) &&
		write_manifest(manifest, frame, type, scope) && !rename(temp, ready.c_str()))
	{
		printf("Screenshot artwork queued: %s\n", ready.c_str());
		return true;
	}

	fprintf(stderr, "Screenshot artwork: could not publish %s\n", ready.c_str());
	unlink(manifest.c_str());
	unlink(png.c_str());
	rmdir(temp);
	return false;
}

void render_menu()
{
	OsdClear();
	OsdSetTitle("Save as artwork", 0);
	if (page == Page::types)
	{
		for (int i = 0; i < type_count; i++)
			OsdWrite(i + 1, artwork_types[i].label, selection == i);
		OsdWrite(15, "Cancel", selection == type_count);
	}
	else if (page == Page::scope)
	{
		OsdWrite(1, artwork_types[selected_type].label);
		for (int i = 0; i < 3; i++)
			OsdWrite(i + 3, scope_labels[i], selection == i);
	}
	else
	{
		switch (displayed_save)
		{
		case SaveState::writing:
			OsdWrite(3, "Saving...");
			break;
		case SaveState::saved:
			OsdWrite(3, "Queued for Core import");
			OsdWrite(5, "OK", 1);
			break;
		case SaveState::failed:
			OsdWrite(3, "Could not save screenshot");
			OsdWrite(5, "Back", 1);
			break;
		}
	}
}

void show_page(Page next, int row)
{
	page = next;
	selection = row;
	render_menu();
}

} // namespace

bool zaparoo_artwork_menu_button(int player, bool pressed)
{
	if (player < 1 || player > NUMPLAYERS) return false;
	auto &deadline = hold_deadlines[player - 1];
	if (pressed)
	{
		deadline = zaparoo_artwork_can_open() ? GetTimer(hold_ms) : 0;
		return false;
	}

	const bool held = deadline && CheckTimer(deadline);
	deadline = 0;
	if (!held || !zaparoo_artwork_can_open()) return false;
	if (capture_state != CaptureState::idle) return true;

	char path[PATH_MAX];
	if (!zaparoo_active_game_read(path, sizeof(path)))
	{
		capture_state = CaptureState::no_game;
		return true;
	}

	capture = std::make_shared<Capture>();
	capture->path = path;
	capture->core = get_rbf_path();
	capture_deadline = GetTimer(capture_timeout_ms);
	capture_state = CaptureState::requested;
	return true;
}

void zaparoo_artwork_capture_frame()
{
	if (capture_state != CaptureState::requested) return;

	capture_state = CaptureState::failed;
	char path[PATH_MAX];
	if (!zaparoo_artwork_can_open() || CheckTimer(capture_deadline) ||
		capture->core != get_rbf_path() || !zaparoo_active_game_read(path, sizeof(path)) || capture->path != path)
		return;

	std::unique_ptr<mister_scaler, decltype(&mister_scaler_free)> scaler(mister_scaler_init(), mister_scaler_free);
	if (!scaler || !scaler->width || !scaler->height || scaler->line < scaler->width * 3) return;

	const uint64_t end = scaler->header + (uint64_t)(scaler->height - 1) * scaler->line + scaler->width * 3;
	const uint64_t bytes = (uint64_t)scaler->width * scaler->height * 4;
	if (end > (unsigned)scaler->num_bytes || bytes > MISTER_SCALER_BUFFERSIZE) return;

	capture->pixels.reset(new (std::nothrow) uint8_t[bytes]);
	if (!capture->pixels || mister_scaler_read(scaler.get(), capture->pixels.get())) return;
	capture->width = scaler->width;
	capture->height = scaler->height;
	capture_state = CaptureState::ready;
}

bool zaparoo_artwork_menu_poll()
{
	if (capture_state == CaptureState::idle) return false;
	if (capture_state == CaptureState::requested && !CheckTimer(capture_deadline)) return false;

	const auto state = capture_state;
	capture_state = CaptureState::idle;
	if (!zaparoo_artwork_can_open())
	{
		capture.reset();
		return false;
	}
	if (state != CaptureState::ready)
	{
		capture.reset();
		Info(state == CaptureState::no_game ? "No active game for artwork" : "Could not capture game frame");
		return false;
	}

	OsdSetSize(16);
	show_page(Page::types, 0);
	OsdEnable(DISABLE_KEYBOARD);
	return true;
}

bool zaparoo_artwork_menu_update(uint32_t key)
{
	const bool cancel = key == (KEY_F12 | UPSTROKE) || key == (KEY_ESC | UPSTROKE);
	const bool back = key == (KEY_BACK | UPSTROKE) || key == (KEY_BACKSPACE | UPSTROKE) || key == KEY_LEFT;
	const bool accept = key == KEY_ENTER || key == KEY_SPACE || key == KEY_KPENTER;
	if (page == Page::save)
	{
		const auto result = capture->save.load();
		if (result != displayed_save)
		{
			displayed_save = result;
			render_menu();
		}
		if (result == SaveState::failed && (accept || back)) show_page(Page::scope, selection);
		return cancel || (result == SaveState::saved && (accept || back));
	}
	if (cancel) return true;

	if (key == KEY_UP || key == KEY_DOWN)
	{
		const int count = page == Page::types ? type_count + 1 : 3;
		selection = (selection + (key == KEY_DOWN ? 1 : count - 1)) % count;
		render_menu();
		return false;
	}
	if (page == Page::types)
	{
		if (back || (accept && selection == type_count)) return true;
		if (selection < type_count && (accept || key == KEY_RIGHT))
		{
			selected_type = selection;
			show_page(Page::scope, 0);
		}
		return false;
	}

	if (back || (accept && selection == 2)) show_page(Page::types, selected_type);
	else if (accept)
	{
		const char *type = artwork_types[selected_type].token;
		const char *scope = scope_tokens[selection];
		capture->save = SaveState::writing;
		displayed_save = SaveState::writing;
		page = Page::save;
		render_menu();
		offload_add_work([frame = capture, type, scope] {
			frame->save = stage_capture(*frame, type, scope) ? SaveState::saved : SaveState::failed;
		});
	}
	return false;
}

void zaparoo_artwork_menu_close()
{
	capture.reset();
}
