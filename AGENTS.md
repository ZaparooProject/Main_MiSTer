# AGENTS.md

This file provides guidance to AI coding agents working with this repository.

---

## !! CRITICAL: This is a Fork — Upstream Sync is Non-Negotiable !!

This repository is a **fork of [MiSTer-devel/Main_MiSTer](https://github.com/MiSTer-devel/Main_MiSTer)** and is **automatically rebased/merged against upstream on a regular basis**. Merge conflicts that require manual resolution are unacceptable — the sync process must remain fully automated.

**Every change you make MUST follow these rules:**

1. **New files over modified files.** Put new functionality in new `.cpp`/`.h` files wherever possible. Avoid modifying upstream source files — a new file has zero merge conflict surface area.

2. **Surgical edits only.** When you must touch an upstream file, make the smallest possible change: add a single `#include`, a single function call, or a single hook point. Never reformat, reorganize, or refactor upstream code.

3. **No reformatting.** Do not reformat, re-indent, or restructure any upstream file. Whitespace-only diffs cause needless conflicts during rebase.

4. **Prefer `support/<core>/` additions.** New per-core functionality belongs in new files under the existing `support/<core>/` directory — the Makefile already globs these in automatically.

5. **Isolate fork-specific code.** If upstream-file edits are unavoidable, wrap them in clearly named macros or `#include` guards (e.g., `#ifdef ZAPAROO`) so conflicts are trivially resolvable and the intent is obvious.

6. **Never touch the Makefile unless strictly necessary.** Makefile conflicts are painful to auto-resolve. Use the existing glob rules instead of adding explicit file references.

**Before making any change, ask yourself:** "If upstream modifies this same file next week, will a `git rebase` auto-resolve cleanly?" If the answer is no, redesign the approach.

---

## Build & Deploy

The binary cross-compiles for ARM (Cyclone V SoC on the DE10-Nano). There is no native x86 build — do not use the system `gcc`.

**Toolchain** (first time only): `source setup_default_toolchain.sh` — must be sourced, not executed. Downloads gcc-arm 10.2-2020.11 and exports `PATH`/`CC`.

```
make              # build bin/MiSTer (stripped) and bin/MiSTer.elf
make DEBUG=1      # O0, -g, no strip
make PROFILING=1  # defines PROFILING macro
make V=1          # echo commands
make clean        # remove bin/
```

**Deploy to device**: `./build.sh` — builds, kills the running `MiSTer` process over SSH, FTPs the binary to `/media/fat/MiSTer`, and relaunches it. Put the device's IP in a `host` file beside the script (default `192.168.1.75`, root/`1`).

There is no automated test suite, linter, or CI.

## Architecture

### Entry & main loop — `main.cpp`

Pins the process to CPU core 1 (core 0 handles Linux IRQs), initialises FPGA I/O, then either enters `scheduler_run()` (`USE_SCHEDULER` defined) or falls into a plain poll loop calling `user_io_poll` / `frame_timer` / `input_poll` / `HandleUI` / `OsdUpdate`.

### Cooperative scheduler — `scheduler.cpp`

Uses **libco** (`lib/libco/arm.c`) to alternate between two cothreads:
- *poll* — `user_io_poll` + `frame_timer` + `input_poll`
- *ui* — `HandleUI` + `OsdUpdate`

Long-running support code must call `scheduler_yield()` to stay cooperative (the coroutine switches back to the scheduler, which picks the other thread).

### FPGA transport — `fpga_io.{h,cpp}`

Memory-maps the Cyclone V HPS→FPGA bridge (base addresses in `fpga_base_addr_ac5.h`, `fpga_*.h`) and exposes `fpga_spi()` for 16-bit SPI words plus fast block-transfer variants. Everything the ARM side sends to any core goes through these functions.

### HPS↔core protocol — `user_io.{h,cpp}` + `spi.{h,cpp}`

`UIO_*` opcodes (defined in `user_io.h`, from `0x00` up) are the command vocabulary: status, joystick axes/buttons, keyboard scancodes, SD sector read/write, video-mode queries, RTC, gamma, etc. The `spi_uio_cmd*` helpers in `spi.h` handle chip-select and framing so callers don't touch the SPI layer directly.

### OSD & menu — `menu.cpp`, `osd.cpp`, `cfg.{h,cpp}`

`menu.cpp` is the state machine for the on-screen display; `osd.cpp` renders it via SPI. `charrom.cpp` provides the font; `logo.h` embeds the logo PNG (see Gotchas). `cfg.{h,cpp}` parses `MiSTer.ini` into the global config structure consumed by video, audio, and menu code.

### Per-core support modules — `support/<core>/`

One directory per supported platform: `minimig`, `snes`, `megadrive`, `n64`, `neogeo`, `megacd`, `psx`, `saturn`, `c64`, `3do`, `atari8bit`, `archie`, `st`, `x86`, `sharpmz`, `cdi`, `pcecd`, `a2`, `uef`, `arcade`, `vhd`, `chd`. The Makefile globs `support/*/*.cpp` — adding a `.cpp` to an existing directory requires no Makefile edit. `support.h` is the umbrella include. Each module owns its own file/disk loaders, save-state glue, and anything not covered by generic `user_io` commands.

### Input — `input.cpp`, `joymapping.cpp`, `autofire.cpp`, `gamecontroller_db.cpp`

Handles evdev, joystick axis/button remapping, auto-fire, and the baked-in SDL-compatible controller DB.

### Video pipeline — `video.cpp`, `scaler.cpp`, `brightness.cpp`, `frame_timer.cpp`

The `.txt` files (`coeff_nn.txt`, `coeff_pp.txt`, `LPF*.txt`, `yc.txt`, `dv_dac*.txt`) are scaler/filter coefficient tables loaded at runtime, not build inputs.

### Disk & image I/O — `file_io.cpp`, `DiskImage.cpp`, `ide.cpp`, `ide_cdrom.cpp`, `cheats.cpp`

CHD images: `lib/libchdr`. Decompression: `lib/miniz`, `lib/lzma`, `lib/zstd`.

### Vendored libraries — `lib/`

`libco`, `miniz`, `lzma`, `zstd`, `libchdr`, `md5`, `bluetooth` are compiled from source. `imlib2` is pre-built (linked with `-Llib/imlib2`). Treat all as third-party — don't refactor them.

## Gotchas

- **PNG assets are linked as binary objects.** `logo.png` → `$(BUILDDIR)/logo.png.o` via `ld -r -b binary`. Renaming the PNG changes the exported symbol that `logo.h` references.
- **`main.cpp.o` has an explicit ordering dep** on all other objects so `-DVDATE` captures a correct build timestamp. Don't remove that Makefile rule.
- **`releases/` contains historical prebuilt binaries**, not source. Exclude it from searches.
- **No x86 build.** To verify logic, read the code or cross-compile and test on hardware.

## References

- Wiki: https://github.com/MiSTer-devel/Wiki_MiSTer/wiki
- Compile prerequisites: https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/
