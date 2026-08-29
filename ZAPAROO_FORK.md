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
| 2 | **Process discovery & gating** | `alt_launcher_installed()` = file exists at `zaparoo/frontend` (cached, keyed on the storage root so `FindStorage()`'s SD-forced `cfg_parse` can't pin it); `alt_launcher_enabled()` = the persisted setting; `alt_launcher_configured()` = installed && enabled && !escaped, and gates spawn/ownership; `alt_launcher_active()` = process running. **OSD surfaces that can re-enable the frontend gate on `installed()`, never `configured()`**, or disabling it becomes a one-way trip | `support/zaparoo/alt_launcher.cpp` |
| 3 | **Custom menu RBF discovery** | `menu_rbf_name()` / `is_menu_rbf()` so a Zaparoo build can ship its own renamed menu RBF and the file_io / fpga_io / user_io paths still recognize it | `support/zaparoo/menu_rbf.cpp/.h`; consumers in `file_io.cpp`, `fpga_io.cpp`, `user_io.cpp` |
| 4 | **Forced cfg overrides** | `alt_launcher_cfg_apply()` forces `cfg.recents = 1` and `cfg.log_file_entry = 1` after INI parse (both back Zaparoo's integration: recents, `/tmp/STARTPATH`, `/tmp/OSD_VISIBLE`, the gameid log), plus `cfg.fb_terminal = 1`. Original `ALT_LAUNCHER` / `MENU_RBF` INI knobs were dropped in favor of file-existence detection | `cfg.cpp:628`, `support/zaparoo/alt_launcher.cpp` (`alt_launcher_cfg_apply`) |
| 5 | **Polling integration** | `alt_launcher_poll()` driven by main scheduler tick | `scheduler.cpp:36`, `support/zaparoo/alt_launcher.cpp:365` |
| 6 | **TTY / framebuffer hygiene** | Clear/reset tty2 around frontend lifecycle; toggle `video_fb_enable` and `video_chvt` only on respawn paths; don't touch them on plain shutdown | `support/zaparoo/alt_launcher.cpp` (`clear_launcher_tty`, `reset_launcher_tty`) |
| 7 | **Joypad routing into frontend** | `alt_launcher_fb_terminal_key()` translates `JOY_L2/R2/OSD` to `KEY_F1/BACKSPACE/MENU`; `joy_digital()` short-circuits to `uinp_send_key` when frontend active | `input.cpp:2475-2484`, `support/zaparoo/alt_launcher.cpp:45-62` |
| 8 | **Native CRT rendering path** | Frontend running in CRT mode: kernel framebuffer at 320×240 RGBA8888, FPGA scans separate region at `0x3A000000`; pre-spawn blank wipes the prior frame. (The v2 contract dropped the old `status[9]` gate: nothing in `support/zaparoo/` writes it) | `support/zaparoo/alt_launcher.cpp` (`enable_native_crt_path`, `disable_native_crt_path`, `blank_native_crt_fb`); paired with `Menu_MiSTer/rtl/native_video_*.sv` |
| 9 | **CRT mode persistence** | 2-byte `zaparoo_launcher_crt.bin` (byte 0 enabled, byte 1 video standard 0 NTSC / 1 480i / 2 PAL) via `FileSaveConfig` / `FileLoadConfig`; loaded at menu init, applied on spawn; the frontend writes the same file and exits 42 to be respawned | `support/zaparoo/alt_launcher.cpp` (`load_persisted_native_crt_state`, `alt_launcher_toggle_native_crt`, `alt_launcher_set_native_crt_mode`) |
| 10 | **Native-core auto-init** | `zaparoo_is_native_core()` matches core name `"Zaparoo Launcher"`; `zaparoo_alt_launcher_init_for_core()` auto-spawns when the FPGA loads that core | `support/zaparoo/alt_launcher.cpp:480-495`, `user_io.cpp:1543` |
| 11 | **In-core "Frontend" OSD entry** | Adds row 31 (`ALT_LAUNCHER_MENUSUB`) to MENU_COMMON1 marked with `reboot_req` when activated | `menu.cpp:2831,2845-2849,3088-3091` |
| 12 | **OSD/F12 overlay over running frontend** | F12 / `KEY_MENU` reaches the OSD even with frontend running; on menu core opens System Settings directly (skip file picker); F1/F9 disabled when frontend active; `vga_nag` suppressed; auto-open suppressed in CRT mode | `menu.cpp:843-852,1289,1304-1311,1334,1583,1604-1611,6727,6739,6816,6901`, `user_io.cpp:4162-4171` |
| 13 | **Trimmed System Settings render** | `alt_launcher_render_system_menu()` overrides MENU_SYSTEM1 body for the alt-launcher path; `alt_launcher_translate_system_select()` maps trimmed menusub indices (Remap, Define joy, Scripts, Zaparoo, Reboot, Exit) to upstream dispatch slots; `-2` enters the Zaparoo page. All three helpers gate on `installed()` and **must agree**, or the render and the dispatch map desync and rows fire the wrong action | `support/zaparoo/alt_launcher_menu.cpp`, `menu.cpp` `MENU_SYSTEM1/2` hooks |
| 14 | **Zaparoo OSD pages** | System Settings → Zaparoo enters `MENU_ZAPAROO_FRONTEND1/2`: rows 0 Frontend on/off, 1 Kiosk mode, 2 Auto-save, blank, 3 CRT mode, 4 Video standard, 5 Screen position, 6 exit (`menumask = crt ? 0x7F : 0x4F`). `menu.cpp` asks `frontend_page_row_has_submenu()` which row Right enters, rather than carrying a row index that moves whenever this page changes. Row 0 reads `alt_launcher_enabled()`, not `configured()`, so it doesn't show Off merely because the user quit the frontend. Row 1 never toggles directly, it enters `MENU_ZAPAROO_KIOSK1/2`, a No/Yes confirmation modelled on `MENU_SCRIPTS_PRE`; confirming closes the OSD itself. Screen position enters `MENU_ZAPAROO_POSITION1/2` (H/V offsets, live adjust). The CRT rows duplicate the frontend's own settings so a user whose frontend cannot display on the CRT can still fix them | `support/zaparoo/launcher_pages.cpp/.h`, `support/zaparoo/crt_settings.cpp/.h`, `menu.cpp` `MENU_ZAPAROO_*` cases (enum members appended at the tail) |
| 15 | **CRT standard / offsets shared with the frontend** | Standard: state-file byte 1 plus `crt_video_standard` in `zaparoo/frontend.toml` `[settings]`; offsets: `crt_h_offset` / `crt_v_offset` in the same section (the frontend treats the file as authoritative). Live adjust rewrites DDR word1 at `0x3A000004`; with no frontend running Main publishes an alignment pattern into DDR slot 0 so centering can be done blind. The toml is edited one line at a time in place (tmp + rename), never rewritten | `support/zaparoo/crt_settings.cpp` (`toml_set`, `crt_offsets_apply_live`, `crt_test_pattern_publish`) |
| 16 | **OSD auto-dismiss on frontend spawn** | `spawn()` calls `MenuHide()` after fork so an OSD still up from CRT toggle / Reboot doesn't trap input once the frontend grabs the input device | `support/zaparoo/alt_launcher.cpp` (end of `spawn`) |
| 17 | **Framebuffer watchdog** | While an HDMI frontend child is alive, `alt_launcher_poll` re-asserts the HPS framebuffer whenever it is found off (throttled to 250 ms). An HDMI hot-plug re-init (`video_reinit` → `video_menu_bg(-1)` → `video_fb_enable(0)`) otherwise leaves the frontend invisible and makes its startup `vmode` probes time out (frontend then renders at native output size) | `support/zaparoo/alt_launcher.cpp` `alt_launcher_poll()` |
| 18 | **Escape-to-stock semantics** | Sticky `s_escaped` flag makes `alt_launcher_configured()` return `false` after a clean exit (code 0 only), so the rest of the session reverts to stock OSD; reboot or an OSD Frontend action resets it | `support/zaparoo/alt_launcher.cpp` (`return_to_normal_mode`, `restart_launcher`) |
| 21 | **OSD auto-open suppressed while a start is queued** | `alt_launcher_owns_screen()` (queued, respawn pending, or running) replaces `alt_launcher_active()` in the menu-core auto-open rule so the stock System Settings OSD no longer flashes over the core pattern between `user_io_init` and the first spawn | `menu.cpp` `MENU_NONE2`, `support/zaparoo/alt_launcher.cpp` |
| 22 | **`load_core menu.rbf` resolves to the fork menu core** | `fpga_load_rbf` builds the path from `menu_rbf_name()` for any menu alias, so Zaparoo's exit-game path (`load_core menu.rbf`) loads `zaparoo/menu_zaparoo.rbf` instead of the stock `menu.rbf` (which has no native CRT path) | `fpga_io.cpp` (`fpga_load_rbf`), `support/zaparoo/menu_rbf.cpp` |
| 23 | **Startup trace** | `zlog()` lines (`alt_launcher t=<ms>: …`, flushed) at init queued, first poll, EDID retry, spawn, finalize, child exit, watchdog, stale sweep — capture with Main's stdout redirected to a file | `support/zaparoo/alt_launcher.cpp` |
| 24 | **Input loop no longer spins on SD activity** | `input_test` drains events until `poll()` idles for 25 ms; the HPS LED's `brightness_hw_changed` attribute (mmc trigger) wakes it at kHz rates during sustained SD-card activity (a media scrape, for example), so the loop never idled and the UI cothread stalled 200–500 ms per pass — dropped OSD keys, laggy menus, delayed launcher polls. An LED-only wakeup now ends the drain | `input.cpp` (`input_test`, 1-line fork edit) |
| 19 | **CI / build infrastructure** | Docker container build; binary named `MiSTer_Zaparoo`; "Z"-suffixed version; release / unstable CI; sync-upstream workflow; deploy script | `docker-build.sh`, `stable-build.sh`, `unstable-build.sh`, `deploy-zaparoo.sh`, `.github/build_*.sh`, `.github/workflows/*.yml` |
| 20 | **`RECENTS` / `LOG_FILE_ENTRY` forced on** | Both are forced in `alt_launcher_cfg_apply()`, not via `cfg.cpp` defaults. `d0357b9` originally used a `min=1` clamp in `ini_vars[]`, which only ever clamped values actually present in the ini, so an absent key still left the flag at 0; `0ebf4b1` then dropped the `LOG_FILE_ENTRY` half entirely while keeping `recents`. Restored as a plain assignment (see row 4) | `support/zaparoo/alt_launcher.cpp` (`alt_launcher_cfg_apply`) |
| 25 | **Fork settings store** | `config/zaparoo_settings.bin`, 16-byte blob read into a zeroed buffer so an absent or short file means today's behavior. Byte 0 = frontend **disabled**, byte 1 = kiosk **enabled**, byte 2 = save on core exit, rest reserved and preserved by the read-modify-write setters. Cached (the predicates run per scheduler tick, per gamepad event and inside the OSD render loop) and keyed on the storage root. Deliberately not `MiSTer.ini`: an unknown key breaks non-fork Mains, upstream appends to `ini_vars[]` most releases, and the stable build excludes `MiSTer.ini` | `support/zaparoo/settings.cpp/.h` |
| 26 | **Kiosk mode** | Total OSD lockout for card-only setups. Gates: `menu.cpp` key decode (covers F1/F7/F9/F10/F11/F12/ESC/Backspace, the front-panel tap and the keyrah Fn combo), the front-panel button block (its 3s hold sets `menustate` directly), the menu-core auto-open, and `SelectINI()` at boot. **The MGL clause in the auto-open condition is deliberately not gated**: it is how a card launches core + ROM, and gating it would load the core but never mount the game. Deliberately does **not** touch the menu background: whatever the user chose via `status[3:1]` (default 0 = the core's own snow, or a wallpaper / test pattern) is what shows. The idle screensaver keeps working to whatever `OSD_TIMEOUT` / `VIDEO_OFF` say: its countdown only runs while `menustate` is the file browser, which kiosk never reaches, so kiosk substitutes `MENU_NONE2` as the idle state, and skips the `OsdMenuCtl(1)` on wake because that would turn the overlay on over stale OSD buffer contents. Recovery is deleting the settings file | `support/zaparoo/kiosk.cpp/.h`, `menu.cpp:626,1341,1640`, `user_io.cpp:1472` |
| 27 | **`zaparoo_` command surface** | `zaparoo_command()` dispatches every `zaparoo_`-prefixed `/dev/MiSTer_cmd` line: `zaparoo_console ...` delegates to `alt_launcher_command()`, plus `zaparoo_kiosk on\|off\|toggle` and `zaparoo_frontend on\|off\|toggle` (both persisted), `zaparoo_osd open\|close\|toggle`, `zaparoo_save [hold_ms]`, `zaparoo_pause on\|off\|toggle` and `zaparoo_mount <slot> [path]`. The upstream hook stays one line: the existing `zaparoo_console ` prefix test was widened to `zaparoo_`. `zaparoo_osd` is the everyday admin route under kiosk: a **session-only** bypass that lifts the gates without changing the setting, raising the same `menu_key_set(KEY_F12 \| UPSTROKE)` user_io raises for a real F12 so it works on a game core too. It is deliberately not persisted and dies on the next core load, since that re-execs Main. Cards must be set up before kiosk is switched on | `support/zaparoo/command.cpp/.h`, `support/zaparoo/kiosk.cpp`, `input.cpp` (cmd FIFO dispatch) |
| 29 | **`OSD_STATUS` is the only save trigger** | **Read this before touching save, pause or the OSD enable path.** Main never initiates a save: `OsdEnable()` sends one SPI byte, the FPGA turns it into `OSD_STATUS`, and the *core's own Verilog* dumps battery RAM over the SD interface on the rising edge, which `user_io_poll` then writes `O_SYNC`. From `sys/osd.v`: `if(!io_din[0]) {osd_status,highres} <= 0; else {osd_status,info} <= {~io_din[2] & ~io_din[3], io_din[2]};` So the status rises **only** with bit 0 set and bits 2 and 3 clear. Bit 2 is `OSD_INFO` and bit 3 is `OSD_MSG`, so **`Info()` (0x45) and `InfoMessage()` (0x49) drive it low** and cannot trigger a save. Status and overlay-enable are the same bit, so raising it always shows the full panel: an invisible save is impossible. `user_io_osd_is_visible()` mirrors it faithfully at every call site except `OsdMenuCtl(1)` | `sys/osd.v` in the core repos, not here; consumers in `support/zaparoo/save.cpp` |
| 30 | **`zaparoo_save` / `zaparoo_pause`** | Forced save and pause, both by asserting `OSD_STATUS` (see row 29), painting a "Saving..." / "Paused" banner first so the panel never shows stale menu content. Save holds until `.sav` sector writes go quiet (200 ms minimum, 400 ms quiet window, 2500 ms cap) rather than a blind delay, because each sector is written `O_SYNC` as it arrives and a truncated dump **corrupts** rather than skips. Covers all three families that need a trigger: generic `FS` cores via the FPGA edge, arcade NVRAM via `UIO_CHK_UPLOAD` + `arcade_nvm_save()`, and N64 64DD via the same `osd_is_visible` edge. An owner enum arbitrates save vs pause, and the real OSD always wins. Saving while paused dips the signal 50 ms first, since a held signal has no rising edge | `support/zaparoo/save.cpp/.h`, `user_io.cpp` (`zaparoo_save_note_write` on the `.sav` write), `scheduler.cpp` (`zaparoo_poll`) |
| 31 | **`zaparoo_mount`** | Disk swap in a running core with no reload, so multi-disk games work under kiosk. **Slots are addressed by 1-based position in the core's declaration order, not by the core's own slot number**, so one card works across cores that number differently; hidden and disabled rows still count so a position never shifts when an unrelated option changes, and the x86/PCXT non-digit slot character is honoured the way `menu.cpp` does. A wrong position logs the core's actual slot list. The resolved number is still range-checked 0-6, because `UIO_SET_SDSTAT` packs it as `(1 << slot)` with bit 7 as write-protect. No path ejects. Dispatch mirrors the `SC` auto-mount path in `user_io_init`, **not** the OSD's, which indexes off the highlighted menu row | `support/zaparoo/mount.cpp/.h`, `support/zaparoo/confstr.cpp/.h` |
| 32 | **Auto-save** | Opt-in (settings byte 2, off by default), behind a `MENU_ZAPAROO_AUTOSAVE1/2` No/Yes confirmation in the same shape as the kiosk one. Only turning it **on** is confirmed; turning it off is harmless and immediate. Flushes the save before the FPGA is reconfigured, hooked as the first statement of `fpga_load_rbf` (the `OsdDisable()` on the next line is a falling edge and useless as a trigger). **Defers the load rather than blocking**: the dump only reaches disk while `user_io_poll` keeps servicing sector writes, a `scheduler_yield()` loop from `co_poll` resumes at the yield point so `user_io_poll` never re-runs, and calling `user_io_poll()` directly recurses because `fpga_load_rbf` is itself called from inside it. The state machine re-issues the load on a clean stack via an `s_reissuing` trampoline. Gated on the core actually having a `.sav`, so cores without one add zero latency. The confirmation page states that it only fires on a core unload and cannot survive a sudden power cut | `support/zaparoo/save.cpp` (`zaparoo_save_defer_core_load`), `fpga_io.cpp` |
| 28 | **Early service start** | A `__attribute__((constructor))` runs `/media/fat/Scripts/zaparoo.sh -service start` before `main()`, so ahead of the core-1 affinity pin (a child would inherit it) and ahead of `FindStorage()`'s 30s USB wait. Double fork + `setsid()` and **no** `PR_SET_PDEATHSIG`, the inverse of `exec_launcher_child`: the service must survive the `app_restart()` re-exec every core load performs. No marker file, the script is an ensure | `support/zaparoo/service_boot.cpp` (zero upstream hooks) |

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
`alt_launcher_cfg_apply()` quietly forces `recents=1`, `log_file_entry=1` and
`fb_terminal` after `cfg_parse`. A user who set any of them in `MiSTer.ini`
gets no warning that we're overriding them.

At minimum, log it once at startup:

```c
printf("alt_launcher: forcing recents/log_file_entry (Zaparoo build)\n");
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
