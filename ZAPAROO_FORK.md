# Zaparoo Fork — Change Map and Cleanup Backlog

This document maps the Zaparoo-specific changes layered on top of MiSTer-devel
`Main_MiSTer`. It is meant to evolve as the fork iterates, so future
contributors don't have to re-derive the architecture from `git log`.

**Scope:** commits authored by `wizzomafizzo` (Callan Barrett) and `asturur`
(Andrea Bogazzi) between commit `a2eb35e` and the tip of
`feat/zaparoo-fb-toggle`. Upstream merges, upstream-author changes, and
fully-reverted experiments (F2 toggle, picker/notice commands, perf tweak,
non-blocking spawn) are intentionally omitted.

> **Living doc:** when you add a Zaparoo-specific behavior, append a row to
> the table below or revise the inconsistencies section. Stale entries are
> worse than no entries — please prune as you cleanup.

---

## 1. Change map

| # | Cluster | Purpose | Where it lives |
|---|---------|---------|----------------|
| 1 | **External launcher process management** | Spawn `zaparoo/launcher` via `agetty` on tty2; SIGTERM/SIGKILL on shutdown; bounded `waitpid`; respawn timer; 3-strike crash give-up | `support/zaparoo/alt_launcher.cpp` (`spawn`, `kill_launcher`, `alt_launcher_poll`, `return_to_normal_mode`) |
| 2 | **Process discovery & gating** | `alt_launcher_configured()` = file exists at `zaparoo/launcher` (cached, with sticky escape bit); `alt_launcher_active()` = process running | `support/zaparoo/alt_launcher.cpp:34-43,329-332` |
| 3 | **Custom menu RBF discovery** | `menu_rbf_name()` / `is_menu_rbf()` so a Zaparoo build can ship its own renamed menu RBF and the file_io / fpga_io / user_io paths still recognize it | `support/zaparoo/menu_rbf.cpp/.h`; consumers in `file_io.cpp`, `fpga_io.cpp`, `user_io.cpp` |
| 4 | **Forced cfg overrides** | `alt_launcher_cfg_apply()` forces `cfg.fb_terminal = 1; cfg.recents = 1` after INI parse. Original `ALT_LAUNCHER` / `MENU_RBF` INI knobs were dropped in favor of file-existence detection | `cfg.cpp:614`, `support/zaparoo/alt_launcher.cpp:24-30` |
| 5 | **Polling integration** | `alt_launcher_poll()` driven by main scheduler tick | `scheduler.cpp:36`, `support/zaparoo/alt_launcher.cpp:365` |
| 6 | **TTY / framebuffer hygiene** | Clear/reset tty2 around launcher lifecycle; toggle `video_fb_enable` and `video_chvt` only on respawn paths; don't touch them on plain shutdown | `support/zaparoo/alt_launcher.cpp` (`clear_launcher_tty`, `reset_launcher_tty`) |
| 7 | **Joypad routing into launcher** | `alt_launcher_fb_terminal_key()` translates `JOY_L2/R2/OSD` to `KEY_F1/BACKSPACE/MENU`; `joy_digital()` short-circuits to `uinp_send_key` when launcher active | `input.cpp:2475-2484`, `support/zaparoo/alt_launcher.cpp:45-62` |
| 8 | **Native CRT rendering path** | Launcher running in CRT mode: kernel framebuffer at 320×240 RGBA8888, FPGA scans separate region at `0x3A000000`, `status[9]=1` gates it; pre-spawn blank wipes the prior frame | `support/zaparoo/alt_launcher.cpp` (`enable_native_crt_path`, `disable_native_crt_path`, `blank_native_crt_fb`); paired with `Menu_MiSTer/rtl/native_video_*.sv` |
| 9 | **CRT mode persistence** | 1-byte `zaparoo_launcher_crt.bin` via `FileSaveConfig` / `FileLoadConfig`; loaded at menu init, applied on spawn | `support/zaparoo/alt_launcher.cpp:76-89,344,499` |
| 10 | **Native-core auto-init** | `zaparoo_is_native_core()` matches core name `"Zaparoo Launcher"`; `zaparoo_alt_launcher_init_for_core()` auto-spawns when the FPGA loads that core | `support/zaparoo/alt_launcher.cpp:480-495`, `user_io.cpp:1543` |
| 11 | **In-core "Launcher" OSD entry** | Adds row 31 (`ALT_LAUNCHER_MENUSUB`) to MENU_COMMON1 marked with `reboot_req` when activated | `menu.cpp:2831,2845-2849,3088-3091` |
| 12 | **OSD/F12 overlay over running launcher** | F12 / `KEY_MENU` reaches the OSD even with launcher running; on menu core opens System Settings directly (skip file picker); F1/F9 disabled when launcher active; `vga_nag` suppressed; auto-open suppressed in CRT mode | `menu.cpp:843-852,1289,1304-1311,1334,1583,1604-1611,6727,6739,6816,6901`, `user_io.cpp:4162-4171` |
| 13 | **Trimmed System Settings render** | `alt_launcher_render_system_menu()` overrides MENU_SYSTEM1 body for the alt-launcher path; `alt_launcher_translate_system_select()` maps trimmed menusub indices to upstream dispatch slots | `support/zaparoo/alt_launcher_menu.cpp`, `menu.cpp:6739-6745,6816-6821` |
| 14 | **Right-side Display Centering page** | New menu state hosting H/V offset adjustment + relocated CRT toggle; persisted via 2-byte `zaparoo_video_offsets.bin`; pushed via `user_io_status_set("[13:10]" / "[17:14]")` | `support/zaparoo/display_menu.cpp/.h` *(local rename in progress: `launcher_pages.cpp/.h`)*, `support/zaparoo/alt_launcher.cpp` (offset state + setters), `menu.cpp` `MENU_ZAPAROO_DISPLAY*` cases |
| 15 | **Escape-to-stock semantics** | Sticky `s_escaped` flag makes `alt_launcher_configured()` return `false` after a clean exit, so the rest of the session reverts to stock OSD; reboot resets it | `support/zaparoo/alt_launcher.cpp:32,38-43,229-241` |
| 16 | **CI / build infrastructure** | Docker container build; binary named `MiSTer_Zaparoo`; "Z"-suffixed version; release / unstable CI; sync-upstream workflow; deploy script | `docker-build.sh`, `stable-build.sh`, `unstable-build.sh`, `deploy-zaparoo.sh`, `.github/build_*.sh`, `.github/workflows/*.yml` |
| 17 | **Build-time defaults flipped** | `cfg.recents` and `LOG_FILE_ENTRY` default to enabled in Zaparoo builds | `cfg.cpp` (defaults) |

---

## 2. Inconsistencies and cleanup backlog

These are intentional starting points for follow-up work, ordered roughly from
"30-minute cleanup" to "needs a design pass."

### 2.1 Mixed namespace prefix in one module
`support/zaparoo/alt_launcher.h` uses two prefixes for the same conceptual thing:

```c
alt_launcher_init / _shutdown / _toggle_crt / _native_crt / _active / _configured ...
zaparoo_is_native_core
zaparoo_alt_launcher_init_for_core / _for_menu
```

The `zaparoo_*` wrappers are the only ones called from `user_io.cpp`. Pick one
prefix (`zap_` is shortest) or split into two headers along that boundary.

### 2.2 Three overlapping predicates
Gating across the codebase uses `_configured()` / `_active()` / `_native_crt()`
interchangeably without a documented rule. Current de-facto convention:

| Predicate | Meaning | Used to gate |
|-----------|---------|--------------|
| `_configured` | binary file exists, sticky off after escape | **render paths** (System Settings body, MENU_SYSTEM1 entry, vga_nag, file-picker entry, "Launcher" row visibility, right-arrow gate) |
| `_active` | PID alive | **input handling** (F1/F9 disable, joypad-to-launcher routing, OSD overlay, menu auto-open suppression) |
| `_native_crt` | PID alive AND CRT mode on | **internal video state machine** (`disable_native_crt_path`, status timer) |

The split is mostly principled but leaks at `MENU_SYSTEM1`, which uses
`_configured` for both the gate AND the body delegation while `MENU_NONE2`
auto-open uses `_active`. A user with the launcher binary present but not yet
running sees different OSD behavior than a user without the binary at all —
worth either a comment or a unified helper.

### 2.3 Two persistence files, no shared format
Current state:

```
zaparoo_launcher_crt.bin     1 byte   (CRT mode flag)
zaparoo_video_offsets.bin    2 bytes  (h_offset, v_offset)
```

Same dir, same `FileSaveConfig` API, but no unified struct. If a third setting
arrives this becomes a per-feature file pattern. A single `zaparoo_state.bin`
with a versioned struct would scale better:

```c
struct zaparoo_state_v1 {
    uint8_t magic;     // 'Z'
    uint8_t version;   // 1
    uint8_t crt;       // 0 / 1
    int8_t  h_offset;  // -8..+7
    int8_t  v_offset;  // -8..+7
    uint8_t reserved[3];
};
```

Cost: a one-shot migration on existing installs (read legacy `crt.bin` if
new file is missing; never write the legacy file again).

### 2.4 Hardcoded paths scattered across modules
- Launcher path: `zaparoo/launcher` in `alt_launcher.cpp:22`
- Menu RBF name(s): hardcoded in `support/zaparoo/menu_rbf.cpp`
- Persistence files: hardcoded in `alt_launcher.cpp`

INI knobs were intentionally dropped (commit `72037bc`) in favor of
file-existence detection, but the resulting literals are now in three places.
A small `support/zaparoo/paths.h` (or a `paths.cpp` that resolves them at
startup) would centralize them without bringing INI knobs back.

### 2.5 Status-bit map exists only in code
`status[9]` (CRT gate), `status[13:10]` (h_offset), `status[17:14]` (v_offset)
are agreed upon between this fork and `Menu_MiSTer/feat/dual-mode-native-fb`,
but the agreement is enforced only by lining up `user_io_status_set("[13:10]", …)`
against the SystemVerilog `status[13:10]`. Neither side has a comment
referencing the other.

A short `STATUS.md` (or a header in `support/zaparoo/`) documenting the shared
register layout would prevent accidental conflicts when a future feature
allocates new bits.

### 2.6 F-key handling comment is archaeology
F2 toggle was added (`0cea191`), moved to `user_io_kbd` (`6d2690b`), and both
were reverted (`390c141`, `4220a82`). What remains is a multi-line comment
block in `user_io.cpp:4162-4171` that reads like commit-history narration.

It can shrink to one line:

```c
// F12/KEY_MENU bypasses alt_launcher_active() so the user can open
// the OSD on top of a running launcher.
```

### 2.7 Trimmed-menu dispatcher has a dead branch
`alt_launcher_translate_system_select()` returns `-1` to signal "consumed
inline." That existed for the CRT row, which has now moved to the right page.

The `if (dispatch < 0) { menustate = MENU_SYSTEM1; break; }` branch in
`MENU_SYSTEM2`'s switch is currently unreachable. Either drop it or note that
it's reserved for future inline-handled rows.

### 2.8 `alt_launcher.cpp` is doing too many jobs
~600 lines covering: process lifecycle + video state machine + tty handling
+ status-bit pushers + offset persistence + cfg overrides. Splitting into
`alt_launcher_proc.cpp`, `alt_launcher_video.cpp`, `alt_launcher_state.cpp`
would make each concern testable and easier to audit. Not urgent — the file
is still readable — but the next feature will tip it over.

### 2.9 Hard-override of user INI config is silent
`alt_launcher_cfg_apply()` quietly forces `fb_terminal=1` and `recents=1`
after `cfg_parse`. A user who set `fb_terminal=0` in `MiSTer.ini` gets no
warning that we're overriding them.

At minimum, log it once at startup:

```c
printf("alt_launcher: forcing fb_terminal=1, recents=1 (Zaparoo build)\n");
```

### 2.10 Naming drift in the new menu page
The right-side page was committed as `MENU_ZAPAROO_DISPLAY1/2` with files
`display_menu.{cpp,h}`. Local in-progress refactor renames the menu states
to `MENU_ZAPAROO_VIDEO*` / `MENU_ZAPAROO_LAUNCHER*` and the file to
`launcher_pages.{cpp,h}`. After the rename settles, double-check that the
file name and the symbols inside agree on what the page is called.

---

## 3. Boundary commit

The "fork divergence" reference point used for this analysis is:

```
a2eb35eacdd7789abe3411e4d03381e7bf55309f
```

Use that as the base in `git log a2eb35e..HEAD` if you want to refresh this
document programmatically.
