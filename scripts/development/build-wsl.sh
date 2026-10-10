#!/usr/bin/env bash
set -euo pipefail
src="$(realpath "$1")"
out="$(realpath -m "$2")"
[[ -f "$src/raspikey/ui/package-lock.json" && -d "$src/.git" ]] ||
    { echo "Pass the RaspiKey checkout and an output directory." >&2; exit 1; }
digest="$(printf '%s' "$src" | sha256sum | cut -c1-12)"
cache="/var/tmp/raspikey-ui-$UID-$digest"
mkdir -p "$cache"
exec 9>"$cache/build.lock"
flock -n 9 || { echo "A frontend build is already running." >&2; exit 1; }
node_dir="$cache/node-v22.13.1-linux-x64"
if [[ ! -x "$node_dir/bin/node" ]]; then
    curl -fsSL https://nodejs.org/dist/v22.13.1/node-v22.13.1-linux-x64.tar.xz -o "$cache/node.tar.xz"
    curl -fsSL https://nodejs.org/dist/v22.13.1/SHASUMS256.txt -o "$cache/SHASUMS256.txt"
    node_digest="$(grep ' node-v22.13.1-linux-x64.tar.xz$' "$cache/SHASUMS256.txt" | cut -d ' ' -f1)"
    printf '%s  %s\n' "$node_digest" "$cache/node.tar.xz" | sha256sum -c -
    tar -xf "$cache/node.tar.xz" -C "$cache"
fi
export PATH="$node_dir/bin:$PATH"
[[ -d "$cache/source/.git" ]] || git clone --no-hardlinks "$src" "$cache/source"
rsync -a --delete --exclude=.git --exclude=.dev --exclude=node_modules --exclude=build --exclude=dist --exclude=__pycache__ "$src/" "$cache/source/"
export RASPIKEY_SOURCE_DIR="$src"
node "$cache/source/scripts/development/build-bundle.cjs" "$out"
