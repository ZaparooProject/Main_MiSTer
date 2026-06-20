# Zaparoo fork release channels

This fork auto-syncs `upstream/master` every day. Two long-lived branches feed two
release channels, so upstream syncing and fork-feature promotion are decoupled.

| Branch   | Feeds              | Release tag               | Prerelease? | Who gets it                        |
|----------|--------------------|---------------------------|-------------|------------------------------------|
| `master` | unstable channel   | `MiSTer_Zaparoo_unstable` | yes         | opt-in testers / nightly users     |
| `stable` | stable channel     | `MiSTer_Zaparoo_YYYYMMDD` | no          | the MiSTer distribution (everyone) |

- `master` is the integration/beta branch. New fork features (PRs) land here and
  immediately reach **only** the unstable prerelease.
- `stable` carries only **promoted** fork features. It is what the distribution ships.
- Both branches keep merging `upstream/master` (see `.github/sync_upstream.sh`), so each
  channel stays current with upstream. This is required: each channel's build layers the
  fork diff `upstream/master..<branch>` onto an upstream base, and that diff is only
  fork-only while the branch contains upstream's commits.

## How a build is assembled

- **Unstable** (`.github/build_unstable_release.sh` → `unstable-build.sh`): upstream `master`
  HEAD base + `git diff upstream/master..master`.
- **Stable** (`.github/build_release.sh` → `stable-build.sh`): upstream's latest tagged
  release (`releases/MiSTer_*`) base + `git diff upstream/master..stable`, applied with
  `git apply -3` (3-way, tolerates upstream drift) — `MiSTer.ini` is the only exclusion.

Each channel is built from a checkout of its **own branch**, so its content, build scripts,
and hook list all come from that branch.

## Triggers

- Push to `master` → rebuild the unstable prerelease only.
- Push to `stable` → re-cut the stable distribution release only.
- Daily `Sync + Stable Release` workflow → merge upstream into both branches, then rebuild
  both channels (skipping a channel whose release already exists for the same inputs).

## Promoting a feature to stable

When a beta feature on `master` is ready to ship to everyone:

```sh
git checkout stable
git merge master            # or cherry-pick specific commits for a partial promotion
git push origin stable      # triggers a fresh MiSTer_Zaparoo_YYYYMMDD stable release
```

The stable build applies the **full** fork diff with `git apply -3`, so hooks in `input.cpp`
/ `scheduler.cpp` promote exactly like any other fork-touched file — there is no separate
hook list to maintain. If upstream ever drifts those files enough that the 3-way apply
conflicts, the stable build fails loudly (rather than silently shipping a stale hook set).
