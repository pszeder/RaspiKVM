#!/usr/bin/env bash
# Sourced by every privileged development operation.
set -euo pipefail
[[ "$(id -u)" == 0 ]] || { echo "Run through sudo." >&2; exit 1; }
[[ "$(hostname)" == raspikey-dev ]] || { echo "Refusing: hostname must be raspikey-dev." >&2; exit 1; }
if [[ ! -r /proc/device-tree/model ]] || ! grep -aq 'Raspberry Pi Zero W' /proc/device-tree/model; then
    echo "Refusing: this installer targets a Pi Zero W only." >&2
    exit 1
fi
[[ "$(dpkg --print-architecture)" == armhf ]] || { echo "Use Raspberry Pi OS 32-bit." >&2; exit 1; }
. /etc/os-release
[[ "${VERSION_CODENAME:-}" == trixie ]] || { echo "Use the current Raspberry Pi OS Lite Trixie image." >&2; exit 1; }
