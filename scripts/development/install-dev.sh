#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
bundle="$(cd -- "${1:-$here/../..}" && pwd)"
tools="$bundle/scripts/development"
setup_mode=false
if [[ "$here" != /usr/local/libexec/raspikey ]]; then
    setup_mode=true
else
    # Normal deployment never replaces or executes uploaded privileged tooling.
    tools="$here"
fi
[[ -f "$bundle/manifest.json" && -d "$bundle/html" ]] || { echo "Use a complete development bundle." >&2; exit 1; }
mkdir -p /var/cache/raspikey-build /opt/raspikey/releases /var/lib/raspikey
chmod 700 /var/lib/raspikey
exec 9>/var/cache/raspikey-build/install.lock
flock -n 9 || { echo "Another build/update is running." >&2; exit 1; }
release="$(python3 - "$bundle" <<'PY'
import hashlib, json, pathlib, re, sys
root = pathlib.Path(sys.argv[1]).resolve()
m = json.loads((root / "manifest.json").read_text())
if m.get("schema") != 1 or not re.fullmatch(r"[A-Za-z0-9._-]+", m["release"]):
    raise SystemExit("Invalid release manifest")
for rel, expected in m["files"].items():
    p = (root / rel).resolve()
    if not p.is_relative_to(root) or not p.is_file():
        raise SystemExit("Invalid manifest path")
    if hashlib.sha256(p.read_bytes()).hexdigest() != expected:
        raise SystemExit("Checksum mismatch: " + rel)
print(m["release"])
PY
)"
if ! $setup_mode; then
    tooling_changed=false
    for name in assert-dev.sh enable-bluetooth.sh release-keys.py rollback-dev.sh install-dev.sh \
                apply-runtime.sh unpack-upload.py deploy-control.sh configure-deploy.sh raspikey-dev.service; do
        if ! cmp -s "$bundle/scripts/development/$name" "$here/$name"; then tooling_changed=true; fi
    done
    if $tooling_changed; then
        echo 'Privileged tooling differs; application code/UI will deploy using the installed framework. Use dev.ps1 setup for framework maintenance.'
    fi
fi
bootstrap=false
if [[ ! -f /etc/raspikey-dev-id || -f /etc/raspikey-dev-bootstrap-pending ]]; then bootstrap=true; fi
if $bootstrap; then
    command -v rpi-usb-gadget >/dev/null || { echo "The OS image must include rpi-usb-gadget." >&2; exit 1; }
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config libglib2.0-dev libdbus-1-dev \
        libboost-system-dev bluez curl rsync avahi-daemon python3 rfkill
fi
if $setup_mode; then
    mkdir -p /var/cache/raspikey-build/source /var/cache/raspikey-build/build
    for path in /var/cache/raspikey-build/source /var/cache/raspikey-build/build; do
        [[ "$(readlink -f "$path")" == "$path" ]] || { echo 'Unexpected build cache link.' >&2; exit 1; }
        chown -hR raspidev:raspidev "$path"
    done
fi
# Cache the build on the Pi so subsequent C++ edits rebuild only changed objects.
# Compare contents and retain destination timestamps for unchanged files.
# Bundle creation timestamps must not force a complete native rebuild.
native_changes="$(rsync -rcni --delete "$bundle/raspikey/" /var/cache/raspikey-build/source/)"
rsync -rc --delete "$bundle/raspikey/" /var/cache/raspikey-build/source/
swapfile=/var/cache/raspikey-build/build.swap
swap_created=false
runtime_backup=''
activation_started=false
activation_done=false
old="$(readlink -f /opt/raspikey/current || true)"
cleanup() {
    if $activation_started && ! $activation_done && ! $bootstrap; then
        echo 'Activation failed; restoring the previous release and service configuration.' >&2
        systemctl stop raspikey-dev.service || true
        rsync -a --delete "$runtime_backup/helpers/" /usr/local/libexec/raspikey/
        cp -a "$runtime_backup/raspikey-dev.service" /etc/systemd/system/raspikey-dev.service
        if [[ -f "$runtime_backup/sudoers" ]]; then
            cp -a "$runtime_backup/sudoers" /etc/sudoers.d/raspikey-dev-deploy
        else
            rm -f /etc/sudoers.d/raspikey-dev-deploy
        fi
        ln -sfn "$old" /opt/raspikey/.current-next
        mv -Tf /opt/raspikey/.current-next /opt/raspikey/current
        systemctl daemon-reload
        systemctl start raspikey-dev.service || true
        echo "Restored $old." >&2
    fi
    if $swap_created; then swapoff "$swapfile" && rm -f "$swapfile"; fi
    if [[ -n "$runtime_backup" ]]; then rm -rf -- "$runtime_backup"; fi
}
trap cleanup EXIT
if [[ ( -n "$native_changes" || ! -x /var/cache/raspikey-build/build/raspikey ) &&
      "$(awk '/MemTotal:/ {print $2}' /proc/meminfo)" -lt 800000 &&
      "$(awk '/SwapTotal:/ {print $2}' /proc/meminfo)" -lt 500000 ]]; then
    # A temporary swap file prevents compiler OOM on the 512 MB Zero W.
    (umask 077; dd if=/dev/zero of="$swapfile" bs=1M count=512 status=none)
    mkswap "$swapfile" >/dev/null
    swapon "$swapfile"
    swap_created=true
fi
runuser -u raspidev -- cmake -S /var/cache/raspikey-build/source -B /var/cache/raspikey-build/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG'
runuser -u raspidev -- cmake --build /var/cache/raspikey-build/build --parallel 1
readelf -A /var/cache/raspikey-build/build/raspikey | grep -Eq 'Tag_CPU_arch:.*v6' ||
    { echo "Build did not produce an ARMv6 executable." >&2; exit 1; }
candidate="/opt/raspikey/releases/$release"
if [[ ! -d "$candidate" ]]; then
    stage="$(mktemp -d /opt/raspikey/releases/.staging.XXXXXXXX)"
    install -m 755 /var/cache/raspikey-build/build/raspikey "$stage/raspikey"
    cp -a "$bundle/html" "$stage/html"
    cp "$bundle/manifest.json" "$stage/manifest.json"
    mkdir "$stage/runtime"
    cp "$tools/"{assert-dev.sh,enable-bluetooth.sh,release-keys.py,raspikey-dev.service} "$stage/runtime/"
    { g++ --version; pkg-config --modversion glib-2.0 dbus-1;
      dpkg-query -W build-essential cmake libglib2.0-dev libdbus-1-dev libboost-system-dev;
      sha256sum "$stage/raspikey"; readelf -A "$stage/raspikey";
    } > "$stage/build-info.txt"
    mv "$stage" "$candidate"
fi
install_framework() {
    mkdir -p /usr/local/libexec/raspikey
    if ! cmp -s "$tools/usb-gadget.sh" /usr/local/libexec/raspikey/usb-gadget.sh ||
       ! cmp -s "$tools/raspikey-dev-usb.service" /etc/systemd/system/raspikey-dev-usb.service; then
        touch /etc/raspikey-dev-usb-reboot-required
        echo 'USB descriptor update staged. Reboot the development Pi after setup.'
    fi
    for name in assert-dev.sh enable-bluetooth.sh release-keys.py rollback-dev.sh install-dev.sh \
                apply-runtime.sh unpack-upload.py deploy-control.sh configure-deploy.sh usb-gadget.sh; do
        install -m 755 "$tools/$name" "/usr/local/libexec/raspikey/.$name.next"
        mv -Tf "/usr/local/libexec/raspikey/.$name.next" "/usr/local/libexec/raspikey/$name"
    done
    install -m 644 "$tools/raspikey-dev.service" /usr/local/libexec/raspikey/raspikey-dev.service
    install -m 644 "$tools/raspikey-dev-usb.service" /etc/systemd/system/raspikey-dev-usb.service
    install -m 644 "$bundle/manifest.json" /usr/local/libexec/raspikey/framework-manifest.json
    bash /usr/local/libexec/raspikey/configure-deploy.sh
    systemctl daemon-reload
}
if $bootstrap; then
    [[ -f /etc/raspikey-dev-id ]] || cat /proc/sys/kernel/random/uuid > /etc/raspikey-dev-id
    chmod 644 /etc/raspikey-dev-id
    touch /etc/raspikey-dev-bootstrap-pending
    install_framework
    bash "$tools/apply-runtime.sh" "$candidate"
    install -m 755 "$tools/usb-gadget.sh" /usr/local/libexec/raspikey/
    install -m 644 "$tools/raspikey-dev-usb.service" /etc/systemd/system/
    # Keep the OS's ICS watcher and its NetworkManager profiles. Replace only the gadget module.
    printf 'libcomposite\n' > /etc/modules-load.d/usb-gadget.conf
    grep -q '^dtoverlay=dwc2' /boot/firmware/config.txt ||
        printf '\n[all]\ndtoverlay=dwc2,dr_mode=peripheral\n' >> /boot/firmware/config.txt
    grep -q '^dtoverlay=disable-wifi' /boot/firmware/config.txt ||
        printf '\n[all]\ndtoverlay=disable-wifi\n' >> /boot/firmware/config.txt
    systemctl daemon-reload
    systemctl enable raspikey-dev-usb.service raspikey-dev.service rpi-usb-gadget-ics.service avahi-daemon.service
else
    runtime_backup="$(mktemp -d /var/cache/raspikey-build/runtime.XXXXXXXX)"
    cp -a /usr/local/libexec/raspikey "$runtime_backup/helpers"
    cp -a /etc/systemd/system/raspikey-dev.service "$runtime_backup/raspikey-dev.service"
    if [[ -f /etc/sudoers.d/raspikey-dev-deploy ]]; then
        cp -a /etc/sudoers.d/raspikey-dev-deploy "$runtime_backup/sudoers"
    fi
    activation_started=true
    systemctl stop raspikey-dev.service
    bash "$tools/apply-runtime.sh" "$candidate"
fi
ln -sfn "$candidate" /opt/raspikey/.current-next
mv -Tf /opt/raspikey/.current-next /opt/raspikey/current
if $bootstrap; then
    echo "Installed $release. Run sudo reboot once. Reassign Windows ICS to the new development adapter."
    exit 0
fi
# A failed start must still reach the health check and restore the prior release.
systemctl start raspikey-dev.service || true
healthy=false
for ((i=0;i<30;i++)); do
    if curl -fsS --max-time 2 http://127.0.0.1/info | python3 -c \
       'import json,sys; m=json.load(sys.stdin); sys.exit(0 if m.get("release")==sys.argv[1] and m.get("modernRuntime") else 1)' "$release"; then
        healthy=true
        break
    fi
    sleep 1
done
if ! $healthy; then
    echo 'Health check failed.' >&2
    exit 1
fi
if $setup_mode; then install_framework; fi
if [[ -n "$old" && "$old" != "$candidate" ]]; then
    ln -sfn "$old" /opt/raspikey/.previous-next
    mv -Tf /opt/raspikey/.previous-next /opt/raspikey/previous
fi
activation_done=true
echo "Activated $release; settings and Bluetooth pairings retained."
