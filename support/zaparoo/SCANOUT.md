# Optional frontend scanout slots and UIO proxy

`scanout.cpp` owns the private startup handshake; `alt_launcher.cpp` owns child
lifecycle. Protocol v2 grants slot ownership, **never direct FPGA access**. Main
executes complete frontend UIO packets on its cooperatively scheduled poll thread,
without yielding inside a transaction. Its OSD, input and video queries can run
between packets. Do not remove `fpga_io.cpp`'s direct-owner guards or grant access
merely because an offer was exported or the child passed `--latch`.

Slot eligibility is HDMI (not Direct Video), with a profile matching both
the running kernel release and its GNU build ID. Frontend finishes vmode/output probes before requesting access.
Main waits for `finalize_spawn`, checks active mapping conflicts, optionally loads
`/media/fat/zaparoo/modules/<release>/<kernel-build-id>/zaparoo_scanout.ko`, then acknowledges
`ZAPAROO-SCANOUT-2` with `ZAPAROO-SCANOUT-2 PROXY` over the inherited
`SOCK_SEQPACKET` channel. Unknown kernels receive no offer. Both old
Main/new frontend and new Main/old direct-MMIO frontend reject the mismatched
handshake and fall back to fb0. FPGA scanout commands are unchanged.

Native CRT (with or without Direct Video) gets the same socket in a distinct
native mode (`Offer::native`). The request, `finalize_spawn` wait, conflict scan
and module verification are identical; the acknowledgment is
`ZAPAROO-SCANOUT-2 NATIVE`. It tells the frontend the verified module is loaded,
so it may map the Menu core's native video window (`0x3A000000`, 3 MB) and use
the vblank wait through it instead of `/dev/mem`. It grants nothing else: no
slots, no proxy (packets are ignored, `ZAPAROO_SCANOUT_RASTER` is not exported),
and `owned()` / `bus_owned()` stay false, so no HDMI lease behavior switches on.
A denied or absent native offer leaves the frontend on its `/dev/mem` mapping.
Main keeps writing that window's control words through its own `/dev/mem`
mappings; its 3 MB blank after spawn completes before `finalize_spawn`, so
before any native grant. HDMI under Direct Video still receives no offer.

Each subsequent packet contains little-endian u16 magic `0x5A52`, sequence,
command, count and words. Only SET (`0x57`, 12 words), CAPS (`0x59`, 6 words), and
RECEIPT (`0x5B`, 11 words) are accepted. Replies echo the header and replace words
with SPI responses. RASTER (`0x0100`, 2 words) is answered by Main and never
reaches the bus: it returns the width and height of the scaler raster a SET
destination is expressed in. A pixel-repeated mode such as `video_mode=14` keeps
its logical 1280x1440 raster there, which no vmode readback reveals. Main
advertises it with `ZAPAROO_SCANOUT_RASTER=1` in the child environment, so a
frontend never sends it to a Main that would ignore it. Invalid shapes never execute; a frontend deadline prevents
an unresponsive Main from blocking it indefinitely. No child maps FPGA registers.

`owned()` / `alt_launcher_scanout_active()` suppress framebuffer takeover while
slots are live. `bus_owned()` / `alt_launcher_uio_owned()` suppress Main's bus
access only for a direct writer; v2 never grants such a writer. Keep these
predicates separate. Input polling is nonblocking during proxy operation and
noisy-device draining has a per-turn budget, so socket servicing and OSD drawing
are not starved by an attached controller's axis reports.

Ownership ends on socket EOF or after Main stops the child. Frontend disables
the route, unmaps slots, closes their file, then releases the socket. A timed-out
child stop retains ownership and aborts core load, re-exec, restart or script
handoff. Invalid packets do not release a live slot owner. Main reaps its own
asynchronous insmod child; it never unloads a module. Parent-death signaling and
post-fork parent checks prevent frontend survival after Main.

Active `mem_wc`, MagiK, Zaparoo and overlapping `/dev/mem` checks are conservative
cooperative checks, not a security boundary. The `/dev/mem` check covers the
slots and the native video window for either grant. Raw mappers can race them;
independent concurrent renderers remain unsupported.

Build with the existing ARM GNU toolchain and `make` or `./docker-build.sh`.
`support/zaparoo/tests/integration_test.cpp` is a standalone ARM test for FIFO
framing and proxy validation: compile with the same cross-g++ (`-std=c++14
-static`) and execute on the authorized test device. Do not build the application
with host GCC or modify the Makefile. The Menu fork's
`kernel/scanout-slots/README.md` records module provenance/qualification limits.
Local builds do not authorize deployment, force-loading or release-channel changes.

## Kernel profiles

`scanout_profile.cpp` reads the bounded ELF note stream from `/sys/kernel/notes`
and the seven-line `profile` file beside the selected module. The format is:
magic `ZAPAROO-SCANOUT-PROFILE-1`, release, kernel build ID, module build ID,
module SHA-256, kernel source revision, and `zaparoo-scanout-v2-native`, each
followed by a newline. Hex values are lowercase; no extra fields are accepted.
A v1 profile (`zaparoo-scanout-v1-1080p`) selects no module and gets no offer.

Module hashing runs in the asynchronous loader child using `sha256sum`; hashing
and insmod share an open file descriptor so renaming a package during update
does not substitute a different object. The existing loader timeout bounds both
steps. Before granting ownership, including when a device already exists, Main
checks `/sys/module/zaparoo_scanout/notes/.note.gnu.build-id`. A mismatch fails
back to fb0 and never unloads or force-loads anything. Missing SHA-256 tooling
also fails closed. These checks detect incompatible or damaged installations;
they do not authenticate packages against a privileged attacker.

MagiK's Main-window mapping device participates in the existing conflict check.
Slot addresses, UIO protocol and ownership lifecycle remain unchanged. Unknown builds and old flat module layouts receive no offer.
