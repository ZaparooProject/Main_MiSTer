# Optional frontend scanout lease

`scanout.cpp` owns the private startup handshake; `alt_launcher.cpp` owns child
lifecycle. Surgical `fpga_io.cpp` guards suppress Main's FPGA writes while the
acknowledged child holds its descriptor. Do not grant access merely because an
offer was exported or because the child passed `--latch`.

Initial eligibility is HDMI (not Direct Video), exact release string `6.18.38-MiSTer`. Frontend
finishes its vmode/output probes before requesting access. Main waits for
`finalize_spawn`, checks active mapping conflicts, optionally loads
`/media/fat/zaparoo/modules/6.18.38-MiSTer/zaparoo_scanout.ko`, then replies
`ZAPAROO-SCANOUT-1 OK` on the inherited `SOCK_SEQPACKET` channel. Failed setup
falls back to fb0. Unknown kernels and native CRT receive no offer.

Ownership ends on socket EOF or after Main stops the child. Stop/script paths
must release the lease **before** restoring framebuffer/OSD state, but never
while a live child can still write. A timed-out child stop retains ownership and
aborts the core load, re-exec, restart or script handoff. Unexpected extra packets from an owned
socket do not revoke a live writer. Main reaps its own asynchronous insmod child;
it never unloads a module. The frontend's existing parent-death signal and
post-fork parent checks prevent it surviving Main.

Checks for active `mem_wc`, MagiK, Zaparoo and overlapping `/dev/mem` mappings
are conservative cooperative checks, not a security boundary. Generic raw
mappers can race them. Concurrent independent renderers are unsupported.

Build with the existing ARM GNU toolchain and `make`; do not build the
application with host GCC or modify the Makefile. Preserve Qt and existing
native-video behavior. The paired Menu fork's `kernel/scanout-slots/README.md`
contains module provenance and exact qualification limits. Release packaging and
broader hardware lifecycle acceptance remain separate work.
Do not deploy, force-load a module, or change release channels as part of a
local build. Hardware lifecycle acceptance remains required.
