# RetroAchievements

Native RA support, ported from [odelot/Main_MiSTer](https://github.com/odelot/Main_MiSTer).
The pinned release is in `UPSTREAM`.

## Layout

- `achievements*`, `ra_*.cpp/.h` (except `ra_gate.h`, `ra_menu.h`): odelot's files, unmodified except for `patches/`.
- `lib/rcheevos`: official RetroAchievements/rcheevos, the version odelot bundles.
- `retroachievements.{h,cpp}`: the hooks that upstream files call. Upstream files never call odelot's API directly.
- `retroachievements.mk`: rcheevos sources and include paths, pulled in by the one Makefile line.

## Behavior

- RA only turns on for RA cores, meaning a setname starting with `RA_` or an rbf under `_RA_Cores/`, which is how the RA installer launches them. Standard cores share base names with RA cores, and odelot's init writes hardcore status bits, so nothing else enables it.
- `[RA_*] main=MiSTer_RA` is ignored: this Main stays running and uses its built-in RA. Any other `main=` value still applies.
- HTTPS only, verified against `/media/fat/Scripts/.config/downloader/cacert.pem` (Update All), then `/etc/ssl/certs/cacert.pem`, then curl's default store. This is the same order Zaparoo Core uses. odelot runs curl with `-k`; `patches/ra_http_tls.patch` removes that.
- odelot's debug stall probes and `-rdynamic` are not ported.
- Credentials, `achievement.wav` and RA cores come from the RA installer (`/media/fat/retroachievements.cfg`).

## Updating

```
support/retroachievements/sync-odelot.sh <tag>
```

The script copies odelot's RA files and applies `patches/`, failing if a patch no longer applies. It refreshes `lib/rcheevos` if odelot moved to a new rcheevos version, and checks that it matches odelot's bundled copy. Then it updates `UPSTREAM` and prints odelot's changes to Main files since the previous pin. Port those changes into the hooks by hand. The printout also includes upstream changes that odelot merged in; skip those.

Host test: `g++ support/retroachievements/tests/ra_gate_test.cpp -o /tmp/t && /tmp/t`.
