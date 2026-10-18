#!/usr/bin/env bash
# One-time networking maintenance only; it never restarts the HID application.
set -euo pipefail
expected_id="${1:?Expected development instance UUID required}"
expected_mac="${2:?Expected development USB MAC required}"
[[ $EUID == 0 && "$expected_id" =~ ^[a-f0-9-]{36}$ && "$expected_mac" == 02:77:* ]]
[[ "$(cat /etc/raspikey-dev-id)" == "$expected_id" ]]
[[ "$(cat /sys/class/net/usb0/address)" == "$expected_mac" ]]
profile='RaspiKVM USB management'
backup=/var/backups/raspikvm-usb-network
install -d -m 700 "$backup"
if [[ ! -f "$backup/before.json" ]]; then
    cp -a /etc/NetworkManager/system-connections "$backup/"
    python3 - "$backup/before.json" <<'PY'
import json,subprocess,sys
def run(*args): return subprocess.run(args,text=True,capture_output=True).stdout.strip()
json.dump({'watcherEnabled':run('systemctl','is-enabled','rpi-usb-gadget-ics.service'),
           'watcherActive':run('systemctl','is-active','rpi-usb-gadget-ics.service'),
           'active':run('nmcli','-g','GENERAL.CON-UUID','device','show','usb0')},open(sys.argv[1],'w'))
PY
fi
if ! nmcli -g connection.uuid connection show "$profile" >/dev/null 2>&1; then
    nmcli connection add type ethernet ifname usb0 con-name "$profile" \
        802-3-ethernet.mac-address "$expected_mac" \
        connection.autoconnect yes connection.autoconnect-priority 999 \
        ipv4.method manual ipv4.addresses '10.12.194.1/28,192.168.137.86/24' \
        ipv4.gateway 192.168.137.1 ipv4.dns '192.168.137.1,1.1.1.1' \
        ipv4.route-metric 100 ipv6.method disabled
fi
nmcli connection modify "$profile" connection.autoconnect yes connection.autoconnect-priority 999 \
    ipv4.method manual ipv4.addresses '10.12.194.1/28,192.168.137.86/24' \
    ipv4.gateway 192.168.137.1 ipv4.dns '192.168.137.1,1.1.1.1' ipv6.method disabled
systemctl disable --now rpi-usb-gadget-ics.service
systemctl mask rpi-usb-gadget-ics.service
# Retain the old OS profiles as recovery options; the fixed profile wins at boot.
cat > "$backup/rollback.sh" <<'SH'
#!/bin/bash
set -euo pipefail
[[ "$(cat /etc/raspikey-dev-id)" == "$(cat /var/backups/raspikvm-usb-network/instance-id)" ]]
nmcli connection modify 'RaspiKVM USB management' connection.autoconnect no
old="$(python3 -c 'import json;print(json.load(open("/var/backups/raspikvm-usb-network/before.json"))["active"])')"
nmcli connection up uuid "$old" ifname usb0
systemctl unmask rpi-usb-gadget-ics.service
systemctl enable --now rpi-usb-gadget-ics.service
SH
chmod 700 "$backup/rollback.sh"
printf '%s\n' "$expected_id" > "$backup/instance-id"
echo 'Activating fixed USB addresses. SSH may pause briefly.'
trap 'bash "$backup/rollback.sh"; exit 1' ERR
nmcli --wait 20 connection up "$profile" ifname usb0
trap - ERR
ip -4 -brief address show usb0
ip -4 route
echo 'USB management configured. Control page: http://10.12.194.1/html/index.html'
