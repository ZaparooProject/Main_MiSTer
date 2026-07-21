# Auto-save cartridge SRAM on a timer (SNES and similar cores)

> **Revision 2026-06-11**: every per-core claim below re-verified against the
> MiSTer-devel core sources on GitHub. Major changes from the 2026-04-30 draft:
> the HPS-side first-write gate (old guard #7) is **unimplementable** and is
> replaced by the OSD-menumask guard (guard #7 below); the GBA save-type-picker
> hazard no longer exists in the current core; mounted save writes are O_SYNC
> (durability better than assumed); save tracking is per-SD-slot because N64
> mounts several save files; the "Save NVRAM" label is dropped (no core uses it).

## Context

On MiSTer, cores like SNES, NES, Megadrive, NeoGeo, Saturn, Genesis store SRAM
on a "cartridge" file (`.sav`). The HPS does not decide when to flush — the
core does. Today, users must open the OSD (and on some cores explicitly pick
"Save Backup RAM") for the core to flush its dirty SRAM to disk. People
forget, lose progress, and the workaround is folklore.

We're a Main_MiSTer fork. Goal: add a fork-side feature that periodically
forces the core to flush dirty SRAM, with no upstream-file conflict surface
and no core changes required.

## How saving actually works (SNES, representative)

Confirmed by reading SNES.sv (MiSTer-devel/SNES_MiSTer master) and
Main_MiSTer's HPS code:

```verilog
// SNES.sv
always @(posedge clk_sys) begin
    if (bk_ena && ~OSD_STATUS && bk_save_write)
        bk_pending <= 1'b1;          // SRAM write outside OSD → mark dirty
    else if (bk_state | ~bk_ena)
        bk_pending <= 1'b0;          // cleared after save completes
end

wire bk_save = status[13]                                 // (A) menu picked
             | (bk_pending & OSD_STATUS && status[23]);   // (B) autosave
```

So the SNES core flushes SRAM when **either**:
- (A) `status[13]` rising edge — what the OSD's "Save Backup RAM" item does, OR
- (B) Autosave on (`status[23]`) AND OSD is open AND `bk_pending` is set.

The HPS already drives status bits via `user_io_status_set("[13]", 1)`
(user_io.cpp:545). When the core writes a sector to its mounted .sav,
the HPS catches `UIO_SECTOR_WR` (user_io.cpp:3263–3308) and writes it to
disk; the core's `bk_pending` self-clears. So the disk write side is
already handled — we only need to **trigger** it.

The conf string token for SNES is `D0RD,Save Backup RAM;` — bit 13. Other
cores that expose backup RAM use the same convention with different bit
indices (NES, Megadrive, etc.). All the user normally does is pick that
menu item; the bit toggle is the actual side-effect.

## Critical files

| Path | Why it matters |
| --- | --- |
| `user_io.cpp:545` `user_io_status_set` | Already toggles status bits — our only HPS-side primitive needed. |
| `user_io.cpp:2901` `user_io_read_confstr` / `user_io_get_confstr` | Conf string is cached — we can scan it at game-load time. |
| `scheduler.cpp:38` (existing `alt_launcher_poll()` call) | Existing fork hook in the poll coroutine — exactly the pattern we mirror. |
| `support/zaparoo/alt_launcher.cpp` | Existing fork module precedent: `GetTimer`/`CheckTimer`-driven state machine, no upstream conflict surface. |
| `cfg.cpp` / `cfg.h` | The `MiSTer.ini` knob lives here (single appended lines, current fork style — no ifdefs). |
| `menu.cpp:1950-1957` + `spi_uio_cmd16(UIO_GET_OSDMASK, 0)` | The OSD's H/D hide/disable semantics that guard #7 replicates. |

No upstream `.cpp`/`.h` modification is required beyond a 1-line `#include`
+ 1-line poll call in `scheduler.cpp` (already precedented).

## Recommended approach

New module **`support/zaparoo/auto_save.cpp`** + header. One poll call from
`scheduler.cpp` beside the existing `alt_launcher_poll()` call.

**Behaviour**:

1. On core boot / ROM load, scan the conf string for a backup-RAM
   trigger item. Match by name substring — robust across all
   confirmed cores (SNES/NES/MD/SMS/GBA/NeoGeo/TGFX/Saturn/PSX/N64).
   Candidates:
   - `Save Backup RAM` (SNES, NES, GBA, SMS, MegaDrive, TGFX, Saturn, N64, …)
   - `Save Memory Card` (NeoGeo, PSX — `Save Memory Cards` matches via `strstr`)

   ("Save NVRAM" was in the original list; a 2026-06-11 sweep of MiSTer-devel
   found no core that uses it — CD-i auto-saves its timekeeper NVRAM
   core-side — so it was dropped.)

   Delegate bit extraction to `user_io_status_bits()` so single-hex-char
   and `[N]`/`[N:M]` syntaxes both work. Also collect the matched line's
   `H<x>`/`h<x>`/`D<x>`/`d<x>` hide/disable prefixes — they drive guard #7.
   Cache `(core_name, bit_opt, hd masks)`.

2. On save-image mount (`auto_save_on_save_mounted(index, path)`):
   tracked **per SD slot** (16 slots, mirroring `sd_image[16]`), because N64
   mounts several save files (cart save + controller paks) at different
   indexes and one trigger flushes them all.
   a. Check the file extension against the **deny-list**: if the path ends
      in `.fla` (N64 FlashRAM), disarm the whole core for this mount
      generation and log the reason.
   b. Otherwise capture the absolute path (`getFullPath` — mount names are
      relative to the storage root), take a per-mount anchor snapshot
      (`<save>.mount`, see Recovery layers), record the save's mtime/size,
      arm the settle timer.

3. If a trigger bit was found and no mounted save is denied, run a
   poll-driven timer (default 60 s, configurable via `MiSTer.ini`),
   gated by all seven guards (see Safety considerations):
   - Pulse the bit: `user_io_status_set("[N]", 1)` then
     `user_io_status_set("[N]", 0)` — back to back, exactly as
     menu.cpp:2586 does for the real menu item. No hold time is needed
     (each set is a full status-word SPI transfer the core samples), and
     a sleep would block both libco cothreads.
   - The pulse → core's `bk_save` rising edge → core writes sectors to
     .sav via the existing SD-emu path.
   - If `bk_pending` is 0 (or the equivalent in-core dirty flag), the
     save is a no-op. Cheap.

4. If no trigger bit was found, or the save extension is on the
   deny-list, do nothing — log a one-line reason so we can audit
   coverage from the log.

**Why this beats the alternatives:**

- **Pulsing OSD_STATUS** (`spi_osd_cmd(OSD_CMD_ENABLE)` then `DISABLE`)
  would only work for cores with autosave already enabled, and risks a
  visible OSD flash because the OSD framebuffer would render whatever is
  cached. Status-bit pulse is invisible.
- **Hardcoded per-core bit table** would work but rots — every new core
  with backup RAM needs a code change. Conf-string scanning is
  zero-maintenance.
- **External daemon simulating F12** is crude, has visible OSD, and
  requires a separate process.
- **Modifying the core** (Biduleman/SNES_MiSTer_DirectSave precedent) is
  out of scope — we're a Main_MiSTer fork, not a core fork.

## Cross-core compatibility (verified by reading core sources)

All conf-string lines below re-verified 2026-06-11 against MiSTer-devel
GitHub masters (SNES.sv, PSX.sv, N64.sv, Saturn.sv, GBA.sv fetched in full;
others via conf-string dumps and core search):

| Core | Conf-string SAVE item | Bit | v1 status |
| --- | --- | --- | --- |
| SNES | `D0RC,Load / D0RD,Save Backup RAM; D0ON,Autosave` | 13 | **Supported** — Biduleman's reference platform; `D0` = `~bk_ena` (no-battery games masked, guard #7) |
| NES | `H5D0R7,Save Backup RAM;` | 7 | **Supported** |
| SMS | `H8H9D0R7,Save Backup RAM;` | 7 | **Supported** — chained H8/H9/D0 prefixes all honored by guard #7 |
| Genesis/MegaDrive | `D0RH,Save Backup RAM;` | 17 | **Supported** |
| MegaCD | `D0RH,Save Backup RAM;` | 17 | **Supported** (bonus — mounts save at slot 0 with `pre=1`) |
| NeoGeo | `D4RC,Save Memory Card;` | 12 | **Supported** — `D4` = memcard-enable mask |
| GBA | `D0R[13],Save Backup RAM;` | 13 | **Supported** — `D0` = `~bk_ena`; `bk_ena` only rises once the game writes backup memory or a non-empty save mounts, so guard #7 doubles as a core-accurate first-write gate. SRAM-as-RAM titles are additionally on GBA.sv's hardcoded `sram_quirk` list which forces `bk_ena=0` |
| TurboGrafx16 | `D0R7,Save Backup RAM;` | 7 | **Supported** — same SNES `bk_save` pattern |
| Saturn | `D0R[25],Save Backup RAM;` | 25 | **Supported** — single trigger; core also ignores triggers while `bk_state` is high (core-side re-entrancy guard). Mounts at slot 1 |
| PSX | `RD,Save Memory Cards;` | 13 | **Supported** — `bk_save = status[13]`, rising-edge detected; one 128KB memcard mounted at **slot 2** (`psx.cpp:437`) — per-slot tracking required (the old `use_save` global never covers slot 2) |
| N64 | `R[41],Save Backup RAM; O[42],Autosave,On,Off` | 41 | **Supported except FlashRAM** — mounts **multiple** save files (cart save + controller paks) at incrementing slots (`n64.cpp:440-449`); `bk_save = status[41] \| (OSD_STATUS & ~OSD_STATUS_1 & ~status[42])` |
| 3DO | `D0RD,Save Backup RAM;` | 13 | **Supported** (bonus — slot 0, `pre=1`) |
| Gameboy | `h2RA,Save Backup RAM;` | 10 | **Supported** (bonus — lowercase `h2` = blocked while mask bit 2 *clear*) |
| WonderSwan | `d0rA,Save Backup RAM;` | 42 | **Supported** (bonus — lowercase `r` = ex bit, +32) |
| Pokemon Mini | `d0R[10],Save Backup RAM;` | 10 | **Supported** (bonus) |
| jtcores arcade | `RL,Save Backup RAM;` (JTFRAME_SAVEGAME) | 21 | Matches if the core mounts a save with `pre=1`; otherwise stays inert (fails closed) |
| CD-i | — (auto-saves NVRAM core-side, "NvRAM saved" info) | — | Not needed |

**v1 label-match list (substring, applied to the conf-string token after
the `R<bit>,` prefix):**

```
"Save Backup RAM"    // SNES, NES, GBA, SMS, MD, MegaCD, TGFX, Saturn, N64, 3DO, GB, WS, ...
"Save Memory Card"   // NeoGeo (and PSX — "Save Memory Cards" matches via strstr)
```

### File-extension deny-list (replaces the earlier core-name deny-list)

The only mechanism that needs to refuse a particular ROM is **N64
FlashRAM** (`.fla`). N64 selects save type from a per-ROM database in
`support/n64/n64.cpp:567-601`; the HPS picks the file extension at
mount time:

| Extension | Save type | Pulse-safe? |
| --- | --- | --- |
| `.eep` | EEPROM 4K / 16K | Yes |
| `.sra` | SRAM 32K / 96K | Yes |
| `.cpk` / `.tpk` | Controller Pak / Transfer Pak | Yes |
| `.fla` | FlashRAM (erase/program cycles) | **No — deny** |

The check happens in `auto_save_on_save_mounted(path)`: if the path
ends in `.fla`, disarm the whole core for this mount generation. This means N64
SRAM/EEPROM/Pak games auto-save normally; FlashRAM games leave it to
the user's manual OSD save. No core-name deny-list needed.

This generalises: if a future core exposes a non-idempotent save type
under a known file extension, we add one row here. The mechanism
fails open at the file-extension level, which is the right granularity
because that's where save-type lives in MiSTer's HPS data model.

## Per-core save-type variations (Biduleman feedback)

Same core, different mechanisms per cartridge type. Pulsing the same
status bit can be safe for one save type and corrupting for another.

### GBA — "SRAM-as-RAM" hazard, handled core-side

(2026-06-11 correction: the original draft claimed GBA save type is picked
in the OSD and a mismatch corrupts saves. The current GBA core **auto-detects**
save type — `FLASH1M_V` string scan during ROM copy plus runtime
`save_eeprom/save_sram/save_flash` signals — and there is no save-type menu
item. That hazard no longer exists.)

The real hazard is **"SRAM-as-RAM"**: a small set of GBA games use the
cart's SRAM region as scratch RAM, not as save data. Pulsing a save for
those would write garbage over the .sav.

The current GBA core already defends against this, and exposes the defense
to the HPS:

- `bk_ena <= |save_sz`, and `save_sz` only becomes nonzero when the game
  actually writes backup memory, or a non-empty save image mounts. Until
  then the save item is masked via its `D0` prefix — a true first-write
  gate, computed core-side where SRAM writes are visible.
- Known SRAM-as-RAM titles (Rocky, the DBZ Legacy of Goku series, the
  Classic NES / Famicom Mini series, …) are on GBA.sv's hardcoded
  `sram_quirk` list, which disables backup RAM entirely → `bk_ena=0` →
  item masked.

Guard #7 (the OSD-menumask guard) honors that mask, so auto-save inherits
exactly the core's own judgement of when saving is legitimate.

**Why the original "HPS first-write gate" was dropped:** it gated the first
pulse on having observed a `UIO_SECTOR_WR` to the save image — but cores
only write save sectors *when a save is triggered* (OSD item, core autosave
path, or our pulse). In-game SRAM writes are FPGA-internal and never reach
the HPS. The gate could therefore never open for a user who never opens the
OSD — the exact user this feature exists for. Biduleman's `bk_pending` works
because it is a core wire; the HPS-side equivalent is the menumask, not
sector-write observation.

For SNES-class cores whose `bk_ena` is header-driven rather than
write-gated, the pulse is idempotent anyway: BSRAM holds the content loaded
from the .sav at mount until the game writes it, so an early pulse writes
back what was loaded.

### N64 — per-save-type idempotency

N64 has five save types selected by per-ROM database
(`support/n64/n64.cpp:567-601`). Save type → file extension is fixed
at HPS mount time:

- **EEPROM 4K/16K** (`.eep`) — small, sequential block writes, idempotent. Pulse-safe.
- **SRAM 32K/96K** (`.sra`) — same family as SNES SRAM. Pulse-safe.
- **Controller Pak / Transfer Pak** (`.cpk` / `.tpk`) — separate file, idempotent. Pulse-safe.
- **FlashRAM** (`.fla`) — erase/program cycles. A pulse mid-erase
  corrupts the sector. **Not pulse-safe.**

Mitigation: file-extension deny-list (see Cross-core table above).
Refuse to arm auto-save if the mounted save path ends in `.fla`.
Other extensions go through the standard 7-guard machinery.

Note: N64's `bk_save = status[41] | (OSD_STATUS & ~OSD_STATUS_1 & ~status[42])`
gates the autosave path on OSD-close (different from SNES's
OSD-open + autosave-on). Doesn't matter for us — we drive the
manual-save path (`status[41]` rising edge), which is the
unconditional `(A)` branch and works identically across all cores.

### Saturn — single trigger covers both backup targets

Confirmed via `Saturn.sv`: the core has internal-backup-RAM AND
optional cart-backup-RAM, but both are written to a single mounted
.sav file with the cart-type bit (`status[23:21]`) selecting which
address range is in use. Single trigger (`status[25]`) flushes
whichever target is active. From the HPS side, Saturn looks identical
to SNES — one mount, one bit, idempotent pulse. No special handling.

### PSX — one memcard, mounted at SD slot 2

Confirmed via `PSX.sv` and `support/psx/psx.cpp:431-445`. The verilog
exposes `memcard1_*` and `memcard2_*` interfaces, but `psx_mount_save()`
only mounts ONE 128KB save (memcard 1) — at **SD slot index 2**, not 0.
Trigger is `status[13]`, idempotent, edge-detected, drives the shared
`memcard_save` register.

This is why the implementation tracks saves per SD slot: the upstream
`use_save` global is only set for slot 0 (plus CD-i and Saturn slot 1,
user_io.cpp:2186), so a `use_save`-gated sector-write hook silently never
arms the in-flight guard for PSX. The hook keys on the write's disk index
against the tracked slot set instead.

### Future expansion

Adding a core post-v1 means: (1) confirm its conf-string label appears
in the v1 list, (2) confirm its save-file extension(s) aren't on the
deny-list (and add them if they aren't pulse-safe), (3) test the
seven-guard mechanism on hardware with a known-good ROM, (4) document
any additional per-cartridge hazards.

## Prior art and attribution

This feature directly mirrors **Biduleman**'s
[`SNES_MiSTer_DirectSave`](https://github.com/Biduleman/SNES_MiSTer)
fork (branch `skip-osd-save`), which solves the same problem core-side
by removing the OSD_STATUS gate from `bk_save` and adding `bk_load_done`
+ `~bk_state` guards. Our approach replicates those guards HPS-side so
no core-fork is required, but the safety model is theirs and the
README of `auto_save.cpp` will credit Biduleman explicitly.

Relevant references:
- Biduleman, *SNES_MiSTer_DirectSave* — branch `skip-osd-save`,
  `SNES.sv` `bk_save` modification.
- Biduleman's accompanying r/MiSTerFPGA write-up which lays out the
  load-race and save-in-flight risks.

## Safety considerations

### Honest safety assessment

Biduleman's own warnings boil down to: power-cut mid-write can corrupt
the .sav (especially on exFAT), and naive autosave can race with
ROM-load or with an in-flight save. He flags these because his fork
runs without an upstream test fleet — not because the technique is
unsound. The actual disk-write path is identical to the manual
"Save Backup RAM" path users already use. We are not introducing a
new code path on the way to the SD card; we're just triggering the
existing one more often.

What the increased trigger frequency does change:
- **Larger power-cut exposure window** — proportional to writes/hour.
  Mitigated by a sane interval (default 60 s, tunable) and by the
  no-op behavior when `bk_pending == 0`.
- **More opportunities to race transitions** — fully mitigated by
  the HPS-side guards below; failure to implement any one of them
  recreates a known corruption mode.
- **No new filesystem-level risks** — exFAT corruption on power-cut
  is a generic MiSTer issue independent of this feature.

Verdict: safe **conditional on** all seven guards below being in place
plus the four-layer recovery write. The guards are the work; the pulse
itself is a one-liner.

### Naive vs guarded

The naive "pulse status[N] every 60 s" can corrupt saves in three known ways.
The Biduleman/SNES_MiSTer_DirectSave fork solves these *core-side* by adding
a guard:

```verilog
// Biduleman fork
wire bk_save = status[13]
             | (bk_pending & bk_load_done & ~bk_state & status[23]);
//                            ^^^^^^^^^^^^   ^^^^^^^^^
//                            wait for       not currently
//                            initial load   saving
```

Since we cannot modify cores, we replicate these guards **HPS-side** before
pulsing the bit. The poll-driven state machine refuses to fire unless **all**
of the following hold:

1. **A trigger bit was found** for the running core (conf-string scan
   succeeded) AND no mounted save's file extension is on the
   deny-list (`.fla`).
2. **At least one save image is mounted** (tracked per SD slot via the
   `pre` flag of `user_io_file_mount`; an empty-name mount on a tracked
   slot is the unmount signal — unmount callers pass `pre=0`, so the
   hook must not require `pre` on that path).
3. **At least N seconds have elapsed since ROM-load completion** (settle
   window — default 5 s — to let the core finish reading the .sav back into
   SRAM before we ask it to write). Lower-bound equivalent of `bk_load_done`.
4. **No save is currently in flight**: the HPS observed no `UIO_SECTOR_WR`
   traffic to any tracked save slot in the last M ms (default 1000 ms).
   Equivalent of `~bk_state`. (Saturn additionally guards this core-side:
   triggers are ignored while `bk_state` is high.)
5. **OSD is closed.** If the OSD is open, the user is interacting; the core
   may already be running its own auto-save path (autosave + OSD_STATUS gate
   in the original verilog). Skip — we'd only race with it.
6. **The core is not currently loading a ROM**. `is_menu()` is false and no
   recent file-mount transition.
7. **The save item is not hidden/disabled by the core's menumask**
   (2026-06-11, replaces the unimplementable HPS first-write gate — see the
   GBA section). The conf scan collects the matched line's `H/h/D/d`
   prefixes; before each pulse, read the live mask via
   `spi_uio_cmd16(UIO_GET_OSDMASK, 0)` and skip while uppercase-prefix bits
   are set or lowercase-prefix bits are clear — identical semantics to
   menu.cpp:1950-1957. This is what makes the pulse exactly as selectable
   as the real menu item: `D0` = `~bk_ena` masks no-battery games (SNES
   would otherwise write one sector of 0xFF-thrashed BSRAM into a junk
   .sav), GBA's write-gated `bk_ena` masks untouched/SRAM-as-RAM carts,
   NeoGeo's `D4` masks memcard-disabled states.

If all conditions hold and the auto-save interval has elapsed, run the
**four-layer recovery write** below, then pulse the trigger bit.

### Recovery layers (defence in depth)

A single `.bak` only protects against *one* bad cycle. Silent rolling
corruption (the core writes garbage and we don't notice for several
cycles) defeats it: the next snapshot captures the garbage as the new
`.bak`. The four layers below address that, plus other failure modes.

**Layer 1 — Rolling generations (`.bak.0` .. `.bak.N-1`, default N=3)**

Before each pulse, rotate the existing backups and snapshot the current
.sav at index 0:
- delete `.bak.<N-1>` if present
- `rename(.bak.<i>, .bak.<i+1>)` for i = N-2 .. 0
- atomic copy of current .sav to `.bak.0` (write to `.bak.0.tmp`,
  fsync data, rename, fsync dir)

This means even if the most recent snapshot captured corruption, the
user can roll back N-1 cycles. With N=3 and a 60 s interval that's
~3 minutes of recoverable history; at minute 4 a corruption-cycle
will still have rotated out of `.bak.2`. Increasing N is cheap (saves
are KB to a few hundred KB) but anchor snapshot (Layer 2) is a better
long-term safety net than just adding more rolling slots.

**Layer 2 — Per-mount anchor (`.sav.mount`)**

Taken **once** in `auto_save_on_save_mounted()`, before any pulse fires
this session. Never overwritten until the next mount (next ROM load or
core change). Worst-case recovery anchor: "the save state when you
started playing this ROM." Survives any number of corrupt rolling
cycles. Same atomic write pattern (`.sav.mount.tmp` → fsync →
rename → fsync dir). Skipped if `.sav` doesn't exist yet (first save
of a fresh ROM).

**Layer 3 — Sanity check before snapshot**

Before rotating into `.bak.0`, refuse to snapshot if the current .sav
looks corrupt:
- file is all zeros, OR
- file is all 0xFF, OR
- file is dramatically smaller than the previous `.bak.0`
  (heuristic: < 50% of previous size and previous was > 1 KB).

These are corruption signatures, not legitimate save shrinkage. If any
trigger, **skip the rotation and abort the pulse for this cycle** —
better to keep the older known-good rolling chain intact than to
propagate garbage. Log the reason; retry next interval.

**Layer 4 — External-modification guard**

If `.sav`'s mtime or size changed since our last write
(or last mount, whichever was later) and we didn't cause it, treat it
as "user imported a save via FTP / SD-card swap." Skip the auto-save
cycle entirely — we shouldn't stomp a save the user just put there.
Re-baseline mtime/size on the next mount.

### Atomic copy primitive (used by Layer 1 and Layer 2)

```
open(dst.tmp, O_WRONLY|O_CREAT|O_TRUNC)
copy bytes from src
fsync(fd_out)              // data to platter
close(fd_out)
rename(dst.tmp, dst)        // atomic on ext4; delete-then-rename on exFAT/FAT32
fsync(dirfd)                // metadata to platter (open parent O_DIRECTORY|O_RDONLY, fsync, close)
```

Note: `rename` is **NOT atomic on exFAT/FAT32** (the typical
/media/fat filesystem). On exFAT/FAT32 the rename is two operations
(delete-then-rename); a power cut between them can leave only `.tmp`.
Recovery procedure includes checking `.tmp` siblings if the target is
missing — `.tmp` is itself a complete copy by construction.

### After all four layers pass — pulse the trigger bit

```
user_io_status_set("[N]", 1);
user_io_status_set("[N]", 0);
```

(2026-06-11: the draft held the bit for 10 ms with `usleep`. menu.cpp fires
the real menu item with the two calls back to back — each is a complete
status-word SPI transfer the core samples at clk_sys — and `usleep` would
stall both libco cothreads. The hold was removed.)

Then update the recorded mtime/size (Layer 4 baseline) so the next
cycle's external-modification guard knows what state we left it in.

### Failure handling

If any layer's I/O fails (disk full, permission, IO error, sanity
trigger), abort the pulse — better to skip a save cycle than to
propagate corruption or lose the rolling chain. Log and retry next
interval.

If `bk_pending` was 0 inside the core, the pulse is a no-op (bk_save
still fires but writes nothing dirty — cheap). If `bk_pending` was 1,
the core walks SRAM, the HPS catches `UIO_SECTOR_WR`, and the .sav is
updated.

### Power-loss durability of the actual save write

(2026-06-11 correction: the draft claimed the post-pulse sector writes were
only `fflush`ed and could be lost to a power cut. Wrong — writable save
images are opened with `O_RDWR | O_SYNC` (user_io.cpp:2131), so every
sector the core writes is synchronous to the storage device. The "future
fsync of the mounted fd" follow-up is unnecessary and has been dropped.
The remaining power-cut exposure is the generic exFAT/FAT32 metadata risk,
which the recovery layers cover.)

### Known race conditions (community-reported)

- **Save-during-ROM-load**: hot-loading a different ROM while a save was
  pending can write the previous game's RAM into the new game's .sav.
  Guard #2 + #3 + #6 above prevent this.
- **exFAT corruption on power-cut**: cited in MiSTerFPGA forum threads;
  filesystem-level, not specific to this feature. Saving more often
  *increases* the cut-power exposure window, but the four recovery
  layers mean prior-good saves always survive somewhere
  (rolling .bak chain, per-mount anchor, or `.tmp` siblings). Mention
  in commit message; not a blocker.
- **Save-while-OSD-open double-fire**: the SNES verilog already triggers
  via path (B) when OSD is open + autosave is on. Guard #5 prevents us
  from firing concurrently.
- **SRAM-as-RAM games (GBA)**: a few GBA titles use the cart's SRAM
  region as scratch RAM. The core keeps `bk_ena=0` for these (quirk list +
  write-gated `save_sz`), which masks the save item; guard #7 (menumask)
  prevents us from pulsing while it's masked.
- **GBA save-type mismatch**: no longer applicable — the current core
  auto-detects save type (no OSD picker exists).
- **FlashRAM erase/program cycle (N64)**: pulses mid-erase corrupt the
  sector. Mitigated in v1 by file-extension deny on `.fla` only — N64
  SRAM/EEPROM/Pak ROMs continue to auto-save normally.

### Recovery procedure

If a user reports a corrupted .sav, walk the layers from newest to
oldest until one looks good:

1. **Most recent rolling backup** — `mv <rom>.sav.bak.0 <rom>.sav`.
   Restores state from one auto-save cycle ago.
2. **Older rolling backups** — `mv <rom>.sav.bak.1 <rom>.sav`, then
   `.bak.2`, etc., walking backwards through the rolling chain.
3. **Per-mount anchor** — `mv <rom>.sav.mount <rom>.sav`. Restores
   state from when the user loaded this ROM in the current session.
   Survives even when the entire rolling chain has been overwritten
   with corruption.
4. **`.tmp` siblings** — on exFAT/FAT32, a power cut between data
   write and rename can leave only `<rom>.sav.bak.0.tmp` (or
   `.sav.mount.tmp`). These are themselves complete copies; rename
   them in if the non-tmp file is missing.
5. **Pre-feature .sav** — if all of the above are exhausted,
   corruption predates auto-save involvement; out of scope.

Document this procedure in a short note alongside the feature.

### Why pulsing status[N] is the right choice (vs. OSD_STATUS pulse)

Pulsing status[N] uses path (A) in the verilog — the unconditional save
trigger. It does **not** require the user to have enabled the core's
"Autosave" option. Trade-off: we lose the core's `OSD_STATUS` gate and
must replicate it HPS-side. We do (guard #5).

Pulsing OSD_STATUS (the alternative) would require autosave-on, *and*
flash the OSD framebuffer briefly — visible glitch. Status-bit pulse is
invisible to the user.

## Configuration (`MiSTer.ini`)

Single user-facing knob:

```ini
; Zaparoo fork — cartridge SRAM auto-save (seconds between save attempts; 0 = disabled)
AUTO_SAVE=60
```

`cfg.cpp` / `cfg.h` additions are single appended lines (the fork dropped
its `#ifdef ZAPAROO` guards in the 2026-05 restructure; unconditional
single-line additions are the current house style).

Hardcoded constants (no INI knob — tuning these in the field is not
the user's job):
- `SETTLE_MS = 5000` — wait after mount before first pulse
- `QUIET_MS = 1000` — quiet window after save-image sector writes
- `BAK_GENERATIONS = 3` — rolling-backup chain length

## Resolved design choices

Confirmed with user:
- **OSD policy**: Skip while OSD is open (avoid racing with the core's
  own autosave path B in SNES.sv).
- **v1 scope (cores)**: SNES, NES, SMS, MegaDrive, GBA, NeoGeo, TGFX,
  Saturn, PSX, N64 (except FlashRAM `.fla`). Selection driven by
  conf-string label substring match plus a save-file extension
  deny-list (`.fla`). No core-name deny-list.
- **Config**: single `AUTO_SAVE=N` key in `MiSTer.ini` (seconds, `0` =
  disabled = default). Plain appended lines in `cfg.{cpp,h}`.
- **Menumask guard (2026-06-11, supersedes the first-write gate)**: the
  2026-04-30 `s_first_write_seen` design was discovered to be
  unimplementable (cores only emit save-sector writes when a save is
  triggered, so the gate could never open). Replaced by honoring the save
  item's H/D hide/disable prefixes against the live `UIO_GET_OSDMASK`,
  which carries the core's own `bk_ena`-class gating — including GBA's
  write-gated first-write semantics — to the HPS.
- **Recovery layers (added 2026-04-30)**: four-layer defence-in-depth.
  - Layer 1: rolling generations `.bak.0` .. `.bak.<N-1>`, default N=3.
  - Layer 2: per-mount anchor `.sav.mount`, taken once per ROM-load.
  - Layer 3: sanity check (zeros / 0xFF / drastic shrinkage) refuses
    rotation and aborts the pulse.
  - Layer 4: external-modification guard via mtime/size compare —
    skip if the user changed the file out from under us.
- **Atomic write primitive**: `.tmp` write → `fsync(file)` →
  `rename` → `fsync(dirfd)`. Used for both rolling and anchor
  snapshots. exFAT/FAT32 rename non-atomicity covered by `.tmp`
  recovery in the documented procedure.
- **Pulse-write durability**: resolved 2026-06-11 — mounted save images
  are opened `O_RDWR | O_SYNC`, so post-pulse sector writes are already
  synchronous. No follow-up needed.
- **Attribution**: Credit Biduleman's `SNES_MiSTer_DirectSave` (branch
  `skip-osd-save`) in the new module's header comment and the commit
  message.

## Verification

No automated test suite exists. Verify on hardware:

1. Build: `./docker-build.sh` produces `bin/MiSTer_Zaparoo`. Deploy via
   `./build.sh`.

### Core-coverage matrix

For each of SNES, NES, SMS, MegaDrive, NeoGeo, GBA, TGFX, Saturn, PSX,
and N64 (with a non-FlashRAM ROM):
- Boot core, load a save-capable ROM, save in-game, do **not** open
  OSD. Wait past the auto-save interval (default 60 s).
- `stat /media/fat/saves/<core>/<rom>.sav` — mtime should be recent.
- Confirm `.bak.0` and `.sav.mount` both exist and are non-empty.
- Power-cycle MiSTer, reload the same ROM, confirm save is restored.

### N64 FlashRAM deny

Load an N64 FlashRAM-using ROM (e.g. Paper Mario, Zelda: Majora's
Mask). Confirm log shows the file-extension deny on `.fla` and no
pulses fire. Manual OSD save still works.

### Menumask guard (guard #7)

- Boot SNES with a ROM that has **no battery RAM** (e.g. a launch-era
  action game). The "Save Backup RAM" item is greyed out in the OSD
  (`D0` mask set). Wait 2× the interval: confirm the log shows
  "save item masked by core" and no `.sav`/`.bak` files appear.
- Boot GBA with a **fresh ROM and no existing .sav**, leave it at the
  title screen. `bk_ena` stays low until the game writes backup memory,
  so no pulse may fire. Then save in-game, wait one interval, confirm a
  pulse fires and the `.sav` appears.
- N64 multi-slot: load an SRAM game with a Controller Pak enabled.
  Confirm both save files get `.bak.0` snapshots on a pulse.

### Recovery-layer tests

**Layer 1 — rolling generations**: trigger several auto-save cycles in
sequence. After 4 cycles confirm `.bak.0` (most recent), `.bak.1`,
`.bak.2` exist; the 4-cycles-old snapshot has rotated out. Each
should be a valid save loadable by `mv <bak> <sav>` and reloading.

**Layer 2 — per-mount anchor**: load ROM, save in-game, let several
auto-save cycles run. Compare `.sav.mount` to the original .sav
captured before mount — should match the pre-mount state. Recovery
test: `mv <rom>.sav.mount <rom>.sav`, reload, in-game state should be
the start-of-session snapshot regardless of how many auto-saves have
since rotated.

**Layer 3 — sanity check**: with the core not running, manually zero
out the .sav file (`dd if=/dev/zero of=<rom>.sav bs=1 count=$(stat
-c %s <rom>.sav) conv=notrunc`). Boot core, trigger a pulse cycle.
Confirm log shows "sanity check failed, skipping rotation," `.bak.0`
is **not** rotated, and the pulse is aborted. Repeat with all-0xFF
and a truncated file.

**Layer 4 — external modification**: while a save is mounted and
auto-save is armed, externally modify the .sav (touch with new
mtime, or copy a different file in via FTP). Wait for next interval.
Confirm log shows "external modification detected, skipping" and no
pulse fires until next mount.

### Race tests

- Save in-game, immediately load a different ROM before the auto-save
  timer fires. Confirm the new ROM's .sav is not clobbered.
- Open OSD, leave it open past the auto-save interval. Confirm we do
  not double-fire while the core's own autosave path is active.
- Hot-swap ROMs rapidly. Confirm the settle window prevents writes
  before the new save is loaded.

### Power-loss simulation

- Pull power mid-pulse (between `.bak.0` rename and the post-pulse
  sector writes). Confirm `.bak.0` contains the previous-good state
  and `mv` recovery works.
- On exFAT/FAT32: simulate a cut after `.bak.0.tmp` write but before
  `rename` completes (e.g. `kill -9` MiSTer mid-cycle if reproducible).
  Confirm `mv <rom>.sav.bak.0.tmp <rom>.sav` recovers a complete save.

### Negative paths

- Boot a core *without* backup RAM (e.g. an arcade ROM with no save).
  Confirm no spurious SPI traffic, no `.bak`/`.mount` files, log
  noise limited to "no save-trigger menu item found".
- Set `auto_save=0`. Confirm no pulses fire and no `.bak`/`.mount`
  files are produced.

### Logging

With debug logging on, confirm one log line per: successful pulse,
each skipped-guard condition (menumask holds are latched to one line
until the condition clears), sanity-check trigger, external-modification
skip, and FlashRAM deny. Audit `/tmp/MiSTer.log` after a play session
to validate.
