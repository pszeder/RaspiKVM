#!/usr/bin/env bash
# Explicit root maintenance; ordinary app deployments leave networking alone.
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
python3 -I "$here/configure-usb-management.py" "${1:?Expected instance UUID required}" "${2:?Expected USB MAC required}" "$here"
systemctl daemon-reload
systemctl stop rpi-usb-gadget-ics.service
nmcli general reload conf
nmcli --wait 5 device set usb0 managed no
bash /usr/local/libexec/raspikey/enable-usb-management.sh
echo 'Fixed USB management configured: http://10.12.194.1/html/index.html'
