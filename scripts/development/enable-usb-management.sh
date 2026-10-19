#!/usr/bin/env bash
# Called by the existing USB gadget unit after configfs has created usb0.
set -euo pipefail
[[ $EUID == 0 && -s /etc/raspikey-dev-id ]]
digest="$(sha256sum /etc/raspikey-dev-id | cut -c1-8)"
expected_mac="02:77:${digest:0:2}:${digest:2:2}:${digest:4:2}:02"
if [[ "$(cat /sys/class/net/usb0/address)" != "$expected_mac" ]]; then
    ip link set dev usb0 down
    ip link set dev usb0 address "$expected_mac"
fi
# No carrier, DHCP, host response or Internet Sharing is needed for these IPs.
ip link set dev usb0 up
ip -4 address replace 10.12.194.1/28 dev usb0
ip -4 address replace 192.168.137.86/24 dev usb0
ip -4 route replace default via 192.168.137.1 dev usb0 metric 100
