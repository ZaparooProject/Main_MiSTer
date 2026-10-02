#pragma once

namespace zaparoo_launcher {
// The frontend's input handoff follows video preparation, including when the
// scanout bootstrap keeps fb0 blank. Keep this seam independent of VT/FPGA I/O.
template<typename PrepareVideo, typename SwitchInput>
void handoff(bool native_crt, PrepareVideo prepare_video, SwitchInput switch_input)
{
	if (!native_crt)
		prepare_video();
	// fb0 may never be revealed on accelerated HDMI. Its legacy side
	// effect cannot own input, or uinp_check_key synthesizes key-up.
	switch_input(0);
}
}
