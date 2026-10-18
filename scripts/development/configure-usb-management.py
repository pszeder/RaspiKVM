"""Install static USB networking into the existing gadget unit, without starting it.

Shared by online root setup and offline boot recovery. Does not edit Bluetooth
or application state. No additional service or background network watcher.
"""
import hashlib
import json
import os
import pathlib
import re
import shutil
import sys
import tempfile

NM_CONFIG = b'''[device-raspikvm-usb]
match-device=interface-name:usb0
managed=0

[main]
dns=none
'''
DROPIN = b'''[Unit]
Wants=NetworkManager.service
After=NetworkManager.service

[Service]
# Install fixed IPs after gadget creation, including boots without a USB host.
# Network failure must not take down the already created keyboard and mouse.
ExecStartPost=-/usr/local/libexec/raspikey/enable-usb-management.sh
'''
DNS = b'''# Static resolver for the USB-only development Pi.
nameserver 1.1.1.1
nameserver 192.168.137.1
options timeout:2 attempts:2
'''


def atomic_write(target, data, mode):
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=target.name + '.next.', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(name, mode)
        os.replace(name, target)  # Replace a resolver symlink, not its target.
    finally:
        pathlib.Path(name).unlink(missing_ok=True)


def install(instance, mac, assets, root=pathlib.Path('/')):
    identity_bytes = (root / 'etc/raspikey-dev-id').read_bytes()
    digest = hashlib.sha256(identity_bytes).hexdigest()
    expected_mac = '02:77:' + ':'.join(digest[i:i+2] for i in (0, 2, 4)) + ':02'
    if (not re.fullmatch(r'[a-f0-9-]{36}', instance)
            or identity_bytes.decode().strip() != instance or mac != expected_mac):
        raise ValueError('Refusing: development instance or USB MAC mismatch')
    gadget = root / 'etc/systemd/system/raspikey-dev-usb.service'
    if not gadget.is_file() or 'usb-gadget.sh start' not in gadget.read_text():
        raise ValueError('Expected existing development gadget service')
    helper = (assets / 'enable-usb-management.sh').read_bytes()
    if not helper.startswith(b'#!/usr/bin/env bash\n') or b'10.12.194.1/28' not in helper:
        raise ValueError('Unexpected USB address helper')
    system = root / 'etc/systemd/system'
    watcher = system / 'rpi-usb-gadget-ics.service'
    if (watcher.exists() or watcher.is_symlink()) and not (
            watcher.is_symlink() and os.readlink(watcher) == '/dev/null'):
        raise ValueError('Refusing to replace a custom ICS unit')
    changes = {
        'usr/local/libexec/raspikey/enable-usb-management.sh': (helper, 0o755),
        'etc/NetworkManager/conf.d/90-raspikvm-usb-management.conf': (NM_CONFIG, 0o644),
        'etc/systemd/system/raspikey-dev-usb.service.d/20-fixed-network.conf': (DROPIN, 0o644),
        'etc/resolv.conf': (DNS, 0o644),
    }
    backup = root / 'var/backups/raspikvm-usb-network/gadget-startup'
    backup.mkdir(parents=True, exist_ok=True, mode=0o700)
    if not (backup / 'before.json').exists():
        previous = {}
        for relative in changes:
            path = root / relative
            saved = backup / relative
            previous[relative] = path.exists() or path.is_symlink()
            if previous[relative] and not (saved.exists() or saved.is_symlink()):
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, saved, follow_symlinks=False)
        atomic_write(backup / 'before.json', json.dumps(previous).encode(), 0o600)
    for relative, (data, mode) in changes.items():
        atomic_write(root / relative, data, mode)
    if not watcher.is_symlink():
        watcher.symlink_to('/dev/null')
    for link in system.glob('*.wants/rpi-usb-gadget-ics.service'):
        if link.is_symlink():
            link.unlink()
    wants = system / 'multi-user.target.wants/raspikey-dev-usb.service'
    if not wants.is_symlink() and not wants.exists():
        wants.parent.mkdir(parents=True, exist_ok=True)
        wants.symlink_to('/etc/systemd/system/raspikey-dev-usb.service')
    print('Fixed USB addresses installed in existing gadget startup; no additional service.')


if __name__ == '__main__':
    if os.geteuid() != 0:
        raise SystemExit('Root maintenance required')
    install(sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3]))
