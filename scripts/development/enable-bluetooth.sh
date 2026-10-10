#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
# Fresh Raspberry Pi OS can leave Bluetooth soft-blocked even with BlueZ active.
# Enable only Bluetooth; USB continues to provide networking.
rfkill unblock bluetooth
for ((i=0;i<20;i++)); do
    if bluetoothctl power on && bluetoothctl show | grep -q 'Powered: yes'; then
        exit 0
    fi
    sleep 1
done
echo 'Bluetooth controller did not power on.' >&2
exit 1
