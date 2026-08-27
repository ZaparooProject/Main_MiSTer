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

> **Release process:** see [`support/zaparoo/RELEASE.md`](support/zaparoo/RELEASE.md)
> for the `master` (beta → unstable) vs `stable` (promoted → distribution) channel
> split and how to promote a feature to a public release.

---

## 1. Change map

| # | Cluster | Purpose | Where it lives |
|---|---------|---------|----------------|
| 1 | **External frontend process management** | Fork/exec `zaparoo/frontend` on tty7 with `PR_SET_PDEATHSIG` (dies with Main); SIGTERM/SIGKILL on shutdown; bounded `waitpid`; respawn timer; 3-strike crash give-up that resets once a child has run ≥10 s (so repeated deploy kills never give up); only exit code 0 is a user "escape", signals and non-zero codes respawn; stale `frontend` processes left by a killed Main are swept before every FPGA load | `support/zaparoo/alt_launcher.cpp` (`spawn`, `exec_launcher_child`, `kill_stale_frontends`, `alt_launcher_poll`, `return_to_normal_mode`) |
| 2 | **Process discovery & gating** | `alt_launcher_configured()` = file exists at `zaparoo/frontend` (cached, with sticky escape bit); `alt_launcher_active()` = process running | `support/zaparoo/alt_launcher.cpp:34-43,329-332` |
| 3 | **Custom menu RBF discovery** | `menu_rbf_name()` / `is_menu_rbf()` so a Zaparoo build can ship its own renamed menu RBF and the file_io / fpga_io / user_io paths still recognize it | `support/zaparoo/menu_rbf.cpp/.h`; consumers in `file_io.cpp`, `fpga_io.cpp`, `user_io.cpp` |
| 4 | **Forced cfg overrides** | `alt_launcher_cfg_apply()` forces `cfg.fb_terminal = 1; cfg.recents = 1` after INI parse. Original `ALT_LAUNCHER` / `MENU_RBF` INI knobs were dropped in favor of file-existence detection | `cfg.cpp:614`, `support/zaparoo/alt_launcher.cpp:24-30` |
| 5 | **Polling integration** | `alt_launcher_poll()` driven by main scheduler tick | `scheduler.cpp:36`, `support/zaparoo/alt_launcher.cpp:365` |
| 6 | **TTY / framebuffer hygiene** | Clear/reset tty2 around frontend lifecycle; toggle `video_fb_enable` and `video_chvt` only on respawn paths; don't touch them on plain shutdown | `support/zaparoo/alt_launcher.cpp` (`clear_launcher_tty`, `reset_launcher_tty`) |
| 7 | **Joypad routing into frontend** | `alt_launcher_fb_terminal_key()` translates `JOY_L2/R2/OSD` to `KEY_F1/BACKSPACE/MENU`; `joy_digital()` short-circuits to `uinp_send_key` when frontend active | `input.cpp:2475-2484`, `support/zaparoo/alt_launcher.cpp:45-62` |
| 8 | **Native CRT rendering path** | Frontend running in CRT mode: kernel framebuffer at 320×240 RGBA8888, FPGA scans separate region at `0x3A000000`, `status[9]=1` gates it; pre-spawn blank wipes the prior frame | `support/zaparoo/alt_launcher.cpp` (`enable_native_crt_path`, `disable_native_crt_path`, `blank_native_crt_fb`); paired with `Menu_MiSTer/rtl/native_video_*.sv` |
| 9 | **CRT mode persistence** | 2-byte `zaparoo_launcher_crt.bin` (byte 0 enabled, byte 1 video standard 0 NTSC / 1 480i / 2 PAL) via `FileSaveConfig` / `FileLoadConfig`; loaded at menu init, applied on spawn; the frontend writes the same file and exits 42 to be respawned | `support/zaparoo/alt_launcher.cpp` (`load_persisted_native_crt_state`, `alt_launcher_toggle_native_crt`, `alt_launcher_set_native_crt_mode`) |
| 10 | **Native-core auto-init** | `zaparoo_is_native_core()` matches core name `"Zaparoo Launcher"`; `zaparoo_alt_launcher_init_for_core()` auto-spawns when the FPGA loads that core | `support/zaparoo/alt_launcher.cpp:480-495`, `user_io.cpp:1543` |
| 11 | **In-core "Frontend" OSD entry** | Adds row 31 (`ALT_LAUNCHER_MENUSUB`) to MENU_COMMON1 marked with `reboot_req` when activated | `menu.cpp:2831,2845-2849,3088-3091` |
| 12 | **OSD/F12 overlay over running frontend** | F12 / `KEY_MENU` reaches the OSD even with frontend running; on menu core opens System Settings directly (skip file picker); F1/F9 disabled when frontend active; `vga_nag` suppressed; auto-open suppressed in CRT mode | `menu.cpp:843-852,1289,1304-1311,1334,1583,1604-1611,6727,6739,6816,6901`, `user_io.cpp:4162-4171` |
| 13 | **Trimmed System Settings render** | `alt_launcher_render_system_menu()` overrides MENU_SYSTEM1 body for the alt-launcher path; `alt_launcher_translate_system_select()` maps trimmed menusub indices (Remap, Define joy, Scripts, Frontend, Reboot, Exit) to upstream dispatch slots; `-2` enters the Frontend page | `support/zaparoo/alt_launcher_menu.cpp`, `menu.cpp` `MENU_SYSTEM1/2` hooks |
| 14 | **Frontend OSD pages (CRT fallback)** | System Settings → Frontend enters `MENU_ZAPAROO_FRONTEND1/2` (CRT mode toggle, Video standard NTSC/PAL, Screen position, exit). Screen position enters `MENU_ZAPAROO_POSITION1/2` (H/V offset rows, left/right adjust live, back). Duplicates the frontend's own CRT settings so a user whose frontend cannot display on the CRT can still fix them from the OSD | `support/zaparoo/launcher_pages.cpp/.h`, `support/zaparoo/crt_settings.cpp/.h`, `menu.cpp` `MENU_ZAPAROO_*` cases (enum members appended at the tail) |
| 15 | **CRT standard / offsets shared with the frontend** | Standard: state-file byte 1 plus `crt_video_standard` in `zaparoo/frontend.toml` `[settings]`; offsets: `crt_h_offset` / `crt_v_offset` in the same section (the frontend treats the file as authoritative). Live adjust rewrites DDR word1 at `0x3A000004`; with no frontend running Main publishes an alignment pattern into DDR slot 0 so centering can be done blind. The toml is edited one line at a time in place (tmp + rename), never rewritten | `support/zaparoo/crt_settings.cpp` (`toml_set`, `crt_offsets_apply_live`, `crt_test_pattern_publish`) |
| 16 | **OSD auto-dismiss on frontend spawn** | `spawn()` calls `MenuHide()` after fork so an OSD still up from CRT toggle / Reboot doesn't trap input once the frontend grabs the input device | `support/zaparoo/alt_launcher.cpp` (end of `spawn`) |
| 17 | **Framebuffer watchdog** | While an HDMI frontend child is alive, `alt_launcher_poll` re-asserts the HPS framebuffer whenever it is found off (throttled to 250 ms). An HDMI hot-plug re-init (`video_reinit` → `video_menu_bg(-1)` → `video_fb_enable(0)`) otherwise leaves the frontend invisible and makes its startup `vmode` probes time out (frontend then renders at native output size) | `support/zaparoo/alt_launcher.cpp` `alt_launcher_poll()` |
| 18 | **Escape-to-stock semantics** | Sticky `s_escaped` flag makes `alt_launcher_configured()` return `false` after a clean exit (code 0 only), so the rest of the session reverts to stock OSD; reboot or an OSD Frontend action resets it | `support/zaparoo/alt_launcher.cpp` (`return_to_normal_mode`, `restart_launcher`) |
| 21 | **OSD auto-open suppressed while a start is queued** | `alt_launcher_owns_screen()` (queued, respawn pending, or running) replaces `alt_launcher_active()` in the menu-core auto-open rule so the stock System Settings OSD no longer flashes over the core pattern between `user_io_init` and the first spawn | `menu.cpp` `MENU_NONE2`, `support/zaparoo/alt_launcher.cpp` |
| 22 | **`load_core menu.rbf` resolves to the fork menu core** | `fpga_load_rbf` builds the path from `menu_rbf_name()` for any menu alias, so Zaparoo's exit-game path (`load_core menu.rbf`) loads `zaparoo/menu_zaparoo.rbf` instead of the stock `menu.rbf` (which has no native CRT path) | `fpga_io.cpp` (`fpga_load_rbf`), `support/zaparoo/menu_rbf.cpp` |
| 23 | **Startup trace** | `zlog()` lines (`alt_launcher t=<ms>: …`, flushed) at init queued, first poll, EDID retry, spawn, finalize, child exit, watchdog, stale sweep — capture with Main's stdout redirected to a file | `support/zaparoo/alt_launcher.cpp` |
| 24 | **Input loop no longer spins on SD activity** | `input_test` drains events until `poll()` idles for 25 ms; the HPS LED's `brightness_hw_changed` attribute (mmc trigger) wakes it at kHz rates during sustained SD-card activity (a media scrape, for example), so the loop never idled and the UI cothread stalled 200–500 ms per pass — dropped OSD keys, laggy menus, delayed launcher polls. An LED-only wakeup now ends the drain | `input.cpp` (`input_test`, 1-line fork edit) |
| 19 | **CI / build infrastructure** | Docker container build; binary named `MiSTer_Zaparoo`; "Z"-suffixed version; release / unstable CI; sync-upstream workflow; deploy script | `docker-build.sh`, `stable-build.sh`, `unstable-build.sh`, `deploy-zaparoo.sh`, `.github/build_*.sh`, `.github/workflows/*.yml` |
| 20 | **Build-time defaults flipped** | `cfg.recents` and `LOG_FILE_ENTRY` default to enabled in Zaparoo builds | `cfg.cpp` (defaults) |

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
| `_configured` | binary file exists, sticky off after escape | **render paths** (System Settings body, MENU_SYSTEM1 entry, vga_nag, file-picker entry, "Frontend" row visibility, right-arrow gate) |
| `_active` | PID alive | **input handling** (F1/F9 disable, joypad-to-frontend routing, OSD overlay, menu auto-open suppression) |
| `_native_crt` | PID alive AND CRT mode on | **internal video state machine** (`disable_native_crt_path`, status timer) |

The split is mostly principled but leaks at `MENU_SYSTEM1`, which uses
`_configured` for both the gate AND the body delegation while `MENU_NONE2`
auto-open uses `_active`. A user with the frontend binary present but not yet
running sees different OSD behavior than a user without the binary at all —
worth either a comment or a unified helper.

### 2.3 CRT persistence is split across two files
Current state (shared with the frontend, which owns the format):

```
config/zaparoo_launcher_crt.bin   2 bytes  byte 0 enabled, byte 1 standard (0 NTSC, 1 480i, 2 PAL)
zaparoo/frontend.toml [settings]  crt_video_standard = "ntsc"|"pal", crt_h_offset, crt_v_offset
```

Main reads the state file at menu init and on every CRT spawn (it sizes the
framebuffer from byte 1) and only touches the toml keys with single-line
in-place edits (`crt_settings.cpp`). The standard is therefore stored twice;
they are written together so they cannot drift, but a unified contract on the
frontend side would remove the duplication.

### 2.4 Hardcoded paths scattered across modules
- Frontend path: `zaparoo/frontend` in `alt_launcher.cpp:22`
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
// the OSD on top of a running frontend.
```

### 2.7 Trimmed-menu dispatcher return contract
`alt_launcher_translate_system_select()` returns `-2` for the Frontend row
(enter `MENU_ZAPAROO_FRONTEND1`) and `-1` for out-of-range `menusub`
(re-render). The `-1` case is unreachable in practice since the framework
bounds menusub via `menumask`; it stays as the documented "re-render" code.

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

### 2.10 Naming of new menu pages — resolved
The OSD surface is two pages, `MENU_ZAPAROO_FRONTEND*` (CRT mode, video
standard, link to position) and `MENU_ZAPAROO_POSITION*` (H/V offsets), with
renderers `frontend_page_*` and `position_page_*` in
`support/zaparoo/launcher_pages.{cpp,h}` and the shared-state helpers in
`support/zaparoo/crt_settings.{cpp,h}`. ✅

### 2.11 Two ways to dismiss the OSD on frontend spawn
The frontend's `spawn()` calls `MenuHide()` to drop a still-open OSD before
the frontend grabs input, which is the new safety net (entry 16 in the
table). Independently, `MENU_SYSTEM2`'s F12 handler also forces
`MENU_NONE1` when `alt_launcher_configured()` is true. Both are needed —
spawn-side handles the "Reboot from System Settings" path, F12-side
handles the "user opens and closes OSD without spawning" path — but the
overlap is implicit. Worth a code comment naming each owner.

### 2.12 CRT-mode-on-exit — resolved
The former "any exit in CRT mode drops to HDMI for the session" rule is
gone: exit classification is the same on both paths (exit 0 escapes,
everything else respawns under the persisted mode). ✅

---

## 3. Boundary commit

The "fork divergence" reference point used for this analysis is:

```
a2eb35eacdd7789abe3411e4d03381e7bf55309f
```

Use that as the base in `git log a2eb35e..HEAD` if you want to refresh this
document programmatically.
