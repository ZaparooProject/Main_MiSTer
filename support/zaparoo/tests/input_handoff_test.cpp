#include "../bootstrap.h"
#include "../launcher_handoff.h"
#include <assert.h>
#include <stdio.h>

static void handoff_check(bool native_crt, bool bootstrap_blank)
{
	zaparoo_scanout::Bootstrap bootstrap;
	bootstrap.start(bootstrap_blank, 10);
	if (bootstrap_blank) bootstrap.grant();
	int grabbed = 1;
	int video_calls = 0;
	int input_calls = 0;
	zaparoo_launcher::handoff(native_crt, [&] {
		++video_calls;
		// video_fb_set returns here on accelerated HDMI, before its legacy
		// input_switch(0) side effect. This must not turn a hold into a tap.
		if (bootstrap.hidden()) return;
		grabbed = 0;
	}, [&](int value) {
		assert(native_crt || video_calls == 1);
		++input_calls;
		grabbed = value;
	});
	assert(grabbed == 0);
	assert(input_calls == 1);
	assert(video_calls == (native_crt ? 0 : 1));
	assert(bootstrap.hidden() == bootstrap_blank);
}

int main()
{
	handoff_check(false, true);  // Accelerated HDMI: fb0 remains hidden.
	handoff_check(false, false); // Legacy/fallback HDMI.
	handoff_check(true, false);  // Native CRT.
	puts("PASS: frontend input handoff is independent of framebuffer reveal");
}
