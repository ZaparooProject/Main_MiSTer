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
| 1 | **External frontend process management** | Spawn `zaparoo/frontend` via `agetty` on tty2; SIGTERM/SIGKILL on shutdown; bounded `waitpid`; respawn timer; 3-strike crash give-up | `support/zaparoo/alt_launcher.cpp` (`spawn`, `kill_launcher`, `alt_launcher_poll`, `return_to_normal_mode`) |
| 2 | **Process discovery & gating** | `alt_launcher_configured()` = file exists at `zaparoo/frontend` (cached, with sticky escape bit); `alt_launcher_active()` = process running | `support/zaparoo/alt_launcher.cpp:34-43,329-332` |
| 3 | **Custom menu RBF discovery** | `menu_rbf_name()` / `is_menu_rbf()` so a Zaparoo build can ship its own renamed menu RBF and the file_io / fpga_io / user_io paths still recognize it | `support/zaparoo/menu_rbf.cpp/.h`; consumers in `file_io.cpp`, `fpga_io.cpp`, `user_io.cpp` |
| 4 | **Forced cfg overrides** | `alt_launcher_cfg_apply()` forces `cfg.fb_terminal = 1; cfg.recents = 1` after INI parse. Original `ALT_LAUNCHER` / `MENU_RBF` INI knobs were dropped in favor of file-existence detection | `cfg.cpp:614`, `support/zaparoo/alt_launcher.cpp:24-30` |
| 5 | **Polling integration** | `alt_launcher_poll()` driven by main scheduler tick | `scheduler.cpp:36`, `support/zaparoo/alt_launcher.cpp:365` |
| 6 | **TTY / framebuffer hygiene** | Clear/reset tty2 around frontend lifecycle; toggle `video_fb_enable` and `video_chvt` only on respawn paths; don't touch them on plain shutdown | `support/zaparoo/alt_launcher.cpp` (`clear_launcher_tty`, `reset_launcher_tty`) |
| 7 | **Joypad routing into frontend** | `alt_launcher_fb_terminal_key()` translates `JOY_L2/R2/OSD` to `KEY_F1/BACKSPACE/MENU`; `joy_digital()` short-circuits to `uinp_send_key` when frontend active | `input.cpp:2475-2484`, `support/zaparoo/alt_launcher.cpp:45-62` |
| 8 | **Native CRT rendering path** | Frontend running in CRT mode: kernel framebuffer at 320×240 RGBA8888, FPGA scans separate region at `0x3A000000`, `status[9]=1` gates it; pre-spawn blank wipes the prior frame | `support/zaparoo/alt_launcher.cpp` (`enable_native_crt_path`, `disable_native_crt_path`, `blank_native_crt_fb`); paired with `Menu_MiSTer/rtl/native_video_*.sv` |
| 9 | **CRT mode persistence** | 1-byte `zaparoo_launcher_crt.bin` via `FileSaveConfig` / `FileLoadConfig`; loaded at menu init, applied on spawn | `support/zaparoo/alt_launcher.cpp:76-89,344,499` |
| 10 | **Native-core auto-init** | `zaparoo_is_native_core()` matches core name `"Zaparoo Frontend"` and legacy `"Zaparoo Launcher"`; `zaparoo_alt_launcher_init_for_core()` auto-spawns when the FPGA loads that core | `support/zaparoo/alt_launcher.cpp:480-495`, `user_io.cpp:1543` |
| 11 | **In-core "Frontend" OSD entry** | Adds row 31 (`ALT_LAUNCHER_MENUSUB`) to MENU_COMMON1 marked with `reboot_req` when activated | `menu.cpp:2831,2845-2849,3088-3091` |
| 12 | **OSD/F12 overlay over running frontend** | F12 / `KEY_MENU` reaches the OSD even with frontend running; on menu core opens System Settings directly (skip file picker); F1/F9 disabled when frontend active; `vga_nag` suppressed; auto-open suppressed in CRT mode | `menu.cpp:843-852,1289,1304-1311,1334,1583,1604-1611,6727,6739,6816,6901`, `user_io.cpp:4162-4171` |
| 13 | **Trimmed System Settings render** | `alt_launcher_render_system_menu()` overrides MENU_SYSTEM1 body for the alt-launcher path; `alt_launcher_translate_system_select()` maps trimmed menusub indices (Remap, Define joy, Scripts, Reboot, Exit) to upstream dispatch slots | `support/zaparoo/alt_launcher_menu.cpp`, `menu.cpp:6739-6745,6816-6821` |
| 14 | **Right-side Zaparoo Frontend pages (two-page)** | Right-arrow from System Settings enters `MENU_ZAPAROO_LAUNCHER1/2` (top page: Video, Exit). Selecting Video enters `MENU_ZAPAROO_VIDEO1/2` (sub-page: CRT mode, H Offset, V Offset, Exit) where left/right adjust values and `±` also work. Both pages live in one helper file with split renderers / select handlers per page | `support/zaparoo/launcher_pages.cpp/.h` (`launcher_page_*`, `video_page_*`), `support/zaparoo/alt_launcher.cpp` (offset state + setters), `menu.cpp` `MENU_ZAPAROO_LAUNCHER*` and `MENU_ZAPAROO_VIDEO*` cases |
| 15 | **H/V offset persistence and push** | 2-byte `zaparoo_video_offsets.bin` via `FileSaveConfig` / `FileLoadConfig`; loaded at menu init alongside the CRT byte; values pushed to FPGA via `user_io_status_set("[13:10]" / "[17:14]")` so the change takes effect immediately | `support/zaparoo/alt_launcher.cpp` (`load_persisted_offsets`, `save_persisted_offsets`, `alt_launcher_set_h_offset`/`_v_offset`, `zaparoo_alt_launcher_init_for_menu`) |
| 16 | **OSD auto-dismiss on frontend spawn** | `spawn()` calls `MenuHide()` after fork so an OSD still up from CRT toggle / Reboot doesn't trap input once the frontend grabs the input device | `support/zaparoo/alt_launcher.cpp` (end of `spawn`) |
| 17 | **CRT-mode-on-exit safety** | If the frontend exits while in CRT mode (clean or crashed), drop back to HDMI / normal mode for the rest of this session instead of respawning into CRT — avoids a UX trap where the user just left CRT but the frontend would respawn into it. Persisted preference is left untouched so next reboot honors it | `support/zaparoo/alt_launcher.cpp` `alt_launcher_poll()` post-`waitpid` branch |
| 18 | **Escape-to-stock semantics** | Sticky `s_escaped` flag makes `alt_launcher_configured()` return `false` after a clean exit, so the rest of the session reverts to stock OSD; reboot resets it | `support/zaparoo/alt_launcher.cpp:32,38-43,229-241` |
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

### 2.7 Trimmed-menu dispatcher has a dead branch
`alt_launcher_translate_system_select()` no longer returns `-1` in any
reachable path — the only case that used to (CRT toggle handled inline) is
gone now that CRT lives on the Video sub-page. The remaining `return -1`
for out-of-range `menusub` is dead too since the framework bounds menusub
via `menumask = 0x1F`.

The `if (dispatch < 0) { menustate = MENU_SYSTEM1; break; }` branch in
`MENU_SYSTEM2`'s switch is therefore unreachable. Either drop the `-1`
contract entirely or document that it's reserved for future inline-handled
rows.

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
Earlier note about `MENU_ZAPAROO_DISPLAY*` vs in-progress rename is resolved:
the right-side surface is now two pages, `MENU_ZAPAROO_LAUNCHER*` (top) and
`MENU_ZAPAROO_VIDEO*` (sub-page), with renderers `launcher_page_*` and
`video_page_*` both living in `support/zaparoo/launcher_pages.{cpp,h}`.
File name and symbols agree. ✅

### 2.11 Two ways to dismiss the OSD on frontend spawn
The frontend's `spawn()` calls `MenuHide()` to drop a still-open OSD before
the frontend grabs input, which is the new safety net (entry 16 in the
table). Independently, `MENU_SYSTEM2`'s F12 handler also forces
`MENU_NONE1` when `alt_launcher_configured()` is true. Both are needed —
spawn-side handles the "Reboot from System Settings" path, F12-side
handles the "user opens and closes OSD without spawning" path — but the
overlap is implicit. Worth a code comment naming each owner.

### 2.12 CRT-mode-on-exit drops frontend entirely
Entry 17 in the table: if the frontend exits while in CRT mode, the fork
permanently drops back to HDMI for the session, even on a clean exit (not
just crashes). That's intentional UX to avoid a trap, but it means a user
who exits the frontend in CRT mode and wants to come back has to reboot.
Worth documenting in the frontend's own README, or relaxing the rule for
clean exits if respawn-into-CRT turns out to be valuable.

---

## 3. Boundary commit

The "fork divergence" reference point used for this analysis is:

```
a2eb35eacdd7789abe3411e4d03381e7bf55309f
```

Use that as the base in `git log a2eb35e..HEAD` if you want to refresh this
document programmatically.
