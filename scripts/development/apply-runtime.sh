#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
release="$(readlink -f -- "$1")"
[[ "$release" == /opt/raspikey/releases/* && -x "$release/raspikey" ]] || exit 1
# Older preserved development releases used the same shared startup files.
[[ -d "$release/runtime" ]] || exit 0
mkdir -p /usr/local/libexec/raspikey
for name in assert-dev.sh enable-bluetooth.sh release-keys.py; do
    install -m 755 "$release/runtime/$name" "/usr/local/libexec/raspikey/.$name.next"
    mv -Tf "/usr/local/libexec/raspikey/.$name.next" "/usr/local/libexec/raspikey/$name"
done
install -m 644 "$release/runtime/raspikey-dev.service" /etc/systemd/system/.raspikey-dev.service.next
mv -Tf /etc/systemd/system/.raspikey-dev.service.next /etc/systemd/system/raspikey-dev.service
systemctl daemon-reload
