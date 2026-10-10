#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
[[ -f /etc/raspikey-dev-id ]] || { echo "Missing development identity." >&2; exit 1; }
g=/sys/kernel/config/usb_gadget/raspikey-dev
write_descriptor() {
    # configfs replaces the descriptor on each write, rather than appending.
    # Bash printf can split long output: submit all bytes in one system call.
    python3 - "$1" "$2" <<'PY'
import os, pathlib, sys
path, encoded = sys.argv[1:]
data = bytes.fromhex(encoded.replace('\\x', ''))
fd = os.open(path, os.O_WRONLY)
try:
    if os.write(fd, data) != len(data):
        raise SystemExit('Short HID descriptor write')
finally:
    os.close(fd)
if pathlib.Path(path).read_bytes() != data:
    raise SystemExit('HID descriptor readback mismatch')
PY
}
stop_gadget() {
    [[ -d "$g" ]] || return 0
    [[ ! -s "$g/UDC" ]] || printf '\n' > "$g/UDC"
    rm -f "$g/configs/c.1/rndis.usb0" "$g/configs/c.1/hid.usb0" "$g/configs/c.1/hid.usb1" "$g/os_desc/c.1"
    if [[ -d "$g/functions/hid.usb1" ]]; then rmdir "$g/functions/hid.usb1"; fi
    rmdir "$g/configs/c.1/strings/0x409" "$g/configs/c.1" "$g/functions/hid.usb0" "$g/functions/rndis.usb0" "$g/strings/0x409" "$g"
}
case "${1:-start}" in
    stop) stop_gadget; exit 0 ;;
    start) ;;
    *) echo "Usage: $0 start|stop" >&2; exit 1 ;;
esac
modprobe libcomposite
if ! mountpoint -q /sys/kernel/config; then mount -t configfs configfs /sys/kernel/config; fi
if [[ -d "$g" ]]; then
    if [[ -n "$(cat "$g/UDC")" ]]; then exit 0; fi
    stop_gadget
fi
[[ ! -d /sys/module/g_ether ]] || { echo "Reboot once to replace the bootstrap Ethernet gadget." >&2; exit 1; }
mkdir -p "$g"
cd "$g"
echo 0x1d6b > idVendor
echo 0x0104 > idProduct
echo 0x0202 > bcdDevice
echo 0x0200 > bcdUSB
echo 0xEF > bDeviceClass
echo 0x02 > bDeviceSubClass
echo 0x01 > bDeviceProtocol
mkdir -p strings/0x409 configs/c.1/strings/0x409
cat /etc/raspikey-dev-id > strings/0x409/serialnumber
echo 'RaspiKey Community' > strings/0x409/manufacturer
echo 'RaspiKey Development Adapter' > strings/0x409/product
echo 'RaspiKey USB network, keyboard and mouse' > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower
echo 1 > os_desc/use
echo 0xcd > os_desc/b_vendor_code
echo MSFT100 > os_desc/qw_sign
mkdir -p functions/rndis.usb0
echo RNDIS > functions/rndis.usb0/os_desc/interface.rndis/compatible_id
echo 5162001 > functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id
digest="$(sha256sum /etc/raspikey-dev-id | cut -c1-8)"
prefix="02:77:${digest:0:2}:${digest:2:2}:${digest:4:2}"
echo "$prefix:01" > functions/rndis.usb0/host_addr
echo "$prefix:02" > functions/rndis.usb0/dev_addr
ln -s functions/rndis.usb0 configs/c.1/
ln -s configs/c.1 os_desc/
mkdir -p functions/hid.usb0
echo 1 > functions/hid.usb0/protocol
echo 1 > functions/hid.usb0/subclass
echo 9 > functions/hid.usb0/report_length
# Preserve the original keyboard and consumer-control report descriptors.
write_descriptor '\x05\x01\x09\x06\xa1\x01\x85\x01\x05\x07\x19\xe0\x29\xe7\x15\x00\x25\x01\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x03\x95\x05\x75\x01\x05\x08\x19\x01\x29\x05\x91\x02\x95\x01\x75\x03\x91\x03\x95\x06\x75\x08\x15\x00\x25\x6d\x05\x07\x19\x00\x29\x6d\x81\x00\xc0\x05\x0C\x09\x01\xA1\x01\x85\x02\x05\x0C\x15\x00\x25\x01\x75\x01\x95\x07\x09\xB5\x09\xB6\x09\xB7\x09\xCD\x09\xE2\x09\xE9\x09\xEA\x81\x02\x95\x01\x81\x01\xC0' functions/hid.usb0/report_desc
ln -s functions/hid.usb0 configs/c.1/
mkdir -p functions/hid.usb1
echo 0 > functions/hid.usb1/protocol
echo 0 > functions/hid.usb1/subclass
echo 9 > functions/hid.usb1/report_length
# bInterval=1: full-speed 1 ms / high-speed 125 us. No OUT reports: conserve the
# Zero W's USB endpoints for networking and the existing keyboard LED reports.
echo 1 > functions/hid.usb1/interval
echo 1 > functions/hid.usb1/no_out_endpoint
# ID 1: 16 buttons, signed 16-bit X/Y, vertical wheel and horizontal AC Pan.
# IDs 2/3: standard keyboard/consumer events emitted by extra mouse buttons.
write_descriptor '\x05\x01\x09\x02\xa1\x01\x85\x01\x09\x01\xa1\x00\x05\x09\x19\x01\x29\x10\x15\x00\x25\x01\x75\x01\x95\x10\x81\x02\x05\x01\x09\x30\x09\x31\x16\x01\x80\x26\xff\x7f\x75\x10\x95\x02\x81\x06\x09\x38\x15\x81\x25\x7f\x75\x08\x95\x01\x81\x06\x05\x0c\x0a\x38\x02\x81\x06\xc0\xc0\x05\x01\x09\x06\xa1\x01\x85\x02\x05\x07\x19\xe0\x29\xe7\x15\x00\x25\x01\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x03\x19\x00\x29\xe7\x15\x00\x26\xe7\x00\x75\x08\x95\x06\x81\x00\xc0\x05\x0c\x09\x01\xa1\x01\x85\x03\x19\x00\x2a\xff\x03\x15\x00\x26\xff\x03\x75\x10\x95\x01\x81\x00\xc0' functions/hid.usb1/report_desc
ln -s functions/hid.usb1 configs/c.1/
udevadm settle --timeout=5 || true
controller="$(find /sys/class/udc -mindepth 1 -maxdepth 1 -printf '%f\n' | head -n 1)"
[[ -n "$controller" ]] || { echo "No USB device controller; check dwc2 configuration." >&2; exit 1; }
echo "$controller" > UDC
rm -f /etc/raspikey-dev-bootstrap-pending
rm -f /etc/raspikey-dev-usb-reboot-required
# NetworkManager and the OS ICS watcher continue managing usb0.
