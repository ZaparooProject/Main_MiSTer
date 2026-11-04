#!/usr/bin/env bash
# Import RA files from an odelot/Main_MiSTer release.
#
# Usage: support/retroachievements/sync-odelot.sh <tag>
#
# Copies odelot's root RA sources (achievements*, ra_*) into this directory,
# applies patches/*.patch, refreshes lib/rcheevos from the official
# RetroAchievements repo when odelot's pinned version changes, and records the
# pin in UPSTREAM. odelot's edits to Main's own files are not copied: they are
# printed (old pin -> new pin) so the hooks here can be updated by hand.
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "usage: $0 <odelot tag>" >&2
	exit 1
fi
tag="$1"
repo=odelot/Main_MiSTer
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
rcdir="$root/lib/rcheevos"
pin="$here/UPSTREAM"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

api() { curl -fsSL -H "Accept: application/vnd.github+json" "https://api.github.com/$1"; }

sha="$(api "repos/$repo/commits/$tag" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["sha"])')"
api "repos/$repo/git/trees/$sha?recursive=1" > "$work/tree.json"

# path<TAB>blob-sha for the root RA sources and the bundled rcheevos tree.
python3 -I - "$work/tree.json" > "$work/files.tsv" <<'PY'
import json, re, sys
tree = json.load(open(sys.argv[1]))
if tree.get("truncated"):
    sys.exit("tree listing truncated")
for e in tree["tree"]:
    if e["type"] != "blob":
        continue
    p = e["path"]
    if re.fullmatch(r"(achievements[^/]*|ra_[^/]*)\.(cpp|h)", p) or p.startswith("lib/rcheevos/"):
        print(f"{p}\t{e['sha']}")
PY

grep -v '^lib/rcheevos/' "$work/files.tsv" | cut -f1 > "$work/ra_files"
[[ -s "$work/ra_files" ]] || { echo "no RA files found at $tag" >&2; exit 1; }

mkdir -p "$work/ra"
while read -r f; do
	curl -fsSL "https://raw.githubusercontent.com/$repo/$sha/$f" -o "$work/ra/$f"
done < "$work/ra_files"

rc_version="$(curl -fsSL "https://raw.githubusercontent.com/$repo/$sha/lib/rcheevos/setup.sh" \
	| sed -n 's/^VERSION="\${1:-\(v[0-9.]*\)}"$/\1/p')"
[[ -n "$rc_version" ]] || { echo "cannot read rcheevos version from setup.sh" >&2; exit 1; }

old_sha=""
old_rc=""
if [[ -f "$pin" ]]; then
	old_sha="$(sed -n 's/^commit=//p' "$pin")"
	old_rc="$(sed -n 's/^rcheevos=//p' "$pin")"
fi

if [[ "$rc_version" != "$old_rc" || ! -f "$rcdir/include/rc_client.h" ]]; then
	echo "rcheevos $rc_version"
	curl -fsSL "https://codeload.github.com/RetroAchievements/rcheevos/tar.gz/refs/tags/$rc_version" -o "$work/rc.tgz"
	mkdir "$work/rc"
	tar -xzf "$work/rc.tgz" --strip-components=1 -C "$work/rc"
	rm -rf "$rcdir"
	mkdir -p "$rcdir"
	cp -r "$work/rc/include" "$work/rc/src" "$work/rc/LICENSE" "$rcdir/"
fi

# odelot bundles rcheevos unmodified; refuse to build on a copy that differs.
while IFS=$'\t' read -r f blob; do
	case "$f" in lib/rcheevos/include/*|lib/rcheevos/src/*) ;; *) continue ;; esac
	if [[ ! -f "$root/$f" ]] || [[ "$(git hash-object "$root/$f")" != "$blob" ]]; then
		echo "lib/rcheevos differs from odelot's bundled copy: $f" >&2
		exit 1
	fi
done < "$work/files.tsv"

# Drop vendored files odelot removed, then copy and patch.
for f in "$here"/achievements*.cpp "$here"/achievements*.h "$here"/ra_*.cpp "$here"/ra_*.h; do
	[[ -e "$f" ]] || continue
	b="$(basename "$f")"
	case "$b" in ra_gate.h|ra_menu.h) continue ;; esac
	grep -qxF "$b" "$work/ra_files" || rm -f "$f"
done
cp "$work"/ra/* "$here/"
for p in "$here"/patches/*.patch; do
	[[ -e "$p" ]] || continue
	patch --fuzz=0 --no-backup-if-mismatch -d "$root" -p1 < "$p"
done

printf 'tag=%s\ncommit=%s\nrcheevos=%s\n' "$tag" "$sha" "$rc_version" > "$pin"
echo "imported $repo $tag ($sha)"

if [[ -n "$old_sha" && "$old_sha" != "$sha" ]]; then
	echo
	echo "== odelot changes to Main files, $old_sha..$sha (port into hooks by hand) =="
	api "repos/$repo/compare/$old_sha...$sha" | python3 -I -c '
import json, re, sys
d = json.load(sys.stdin)
for f in d.get("files", []):
    p = f["filename"]
    if re.fullmatch(r"(achievements[^/]*|ra_[^/]*)\.(cpp|h)", p) or p.startswith("lib/rcheevos/") \
            or p in ("README.md", "retroachievements.cfg", "deploy.sh", "achievement.wav"):
        continue
    print(f"--- {p} ({f[\"status\"]}, +{f[\"additions\"]} -{f[\"deletions\"]})")
    print(f.get("patch", "(no patch text)"))
'
fi
