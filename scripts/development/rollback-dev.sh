#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
[[ -f /etc/raspikey-dev-id ]] || { echo "Not initialized as a development Pi." >&2; exit 1; }
root=/opt/raspikey
old="$(readlink -f "$root/previous")"
[[ "$old" == "$root/releases/"* && -x "$old/raspikey" ]] || { echo "No previous release." >&2; exit 1; }
current="$(readlink -f "$root/current")"
systemctl stop raspikey-dev.service
bash "$here/apply-runtime.sh" "$old"
ln -sfn "$old" "$root/.current-next"
mv -Tf "$root/.current-next" "$root/current"
# A failed start must still reach the health check and restore the prior release.
systemctl start raspikey-dev.service || true
healthy=false
for ((i=0;i<30;i++)); do
    if curl -fsS --max-time 2 http://127.0.0.1/info | python3 -c 'import json,sys; m=json.load(sys.stdin); sys.exit(0 if m.get("release")==sys.argv[1] and m.get("modernRuntime") else 1)' "$(basename "$old")"; then healthy=true; break; fi
    sleep 1
done
if ! $healthy; then
    systemctl stop raspikey-dev.service
    bash "$here/apply-runtime.sh" "$current"
    ln -sfn "$current" "$root/.current-next"
    mv -Tf "$root/.current-next" "$root/current"
    systemctl start raspikey-dev.service
    echo "Previous release failed its health check; restored current release." >&2
    exit 1
fi
ln -sfn "$current" "$root/.previous-next"
mv -Tf "$root/.previous-next" "$root/previous"
echo "Rolled back to $old"
