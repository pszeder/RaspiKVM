#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
[[ "${SUDO_USER:-}" == raspidev ]] || { echo 'Use the raspidev deployment account.' >&2; exit 1; }
python3 -I - <<'PY'
import json, pathlib, pwd
config=json.loads(pathlib.Path('/etc/raspikey-dev-deploy.json').read_text())
assert config['user']=='raspidev' and config['uid']==pwd.getpwnam('raspidev').pw_uid
assert config['instanceId']==pathlib.Path('/etc/raspikey-dev-id').read_text().strip(), 'Development identity changed'
PY
operation="${1:-}"
shift || true
case "$operation" in
    status)
        [[ $# == 0 ]] || exit 2
        systemctl is-active raspikey-dev.service raspikey-dev-usb.service
        curl -fsS --max-time 5 http://127.0.0.1/info
        echo
        exit 0 ;;
    install) [[ $# == 2 ]] || exit 2 ;;
    rollback|restart|reboot|poweroff) [[ $# == 0 ]] || exit 2 ;;
    logs)
        [[ $# == 0 ]] || exit 2
        exec journalctl -u raspikey-dev.service -n 80 --no-pager ;;
    *) echo 'Usage: deploy-control.sh install UPLOAD SHA256 | status | rollback | restart | logs | reboot | poweroff' >&2; exit 2 ;;
esac
mkdir -p /var/cache/raspikey-deploy
chmod 700 /var/cache/raspikey-deploy
exec 8>/var/cache/raspikey-deploy/control.lock
flock -n 8 || { echo 'Another development operation is running.' >&2; exit 1; }
case "$operation" in
    install)
        stage="$(python3 -I "$here/unpack-upload.py" "$1" "$2" "$(id -u raspidev)")"
        [[ "$stage" == /var/cache/raspikey-deploy/upload.* ]] || exit 1
        trap 'rm -rf -- "$stage"' EXIT
        bash "$here/install-dev.sh" "$stage"
        ;;
    rollback) bash "$here/rollback-dev.sh" ;;
    restart)
        systemctl restart raspikey-dev.service
        for ((i=0;i<20;i++)); do
            if curl -fsS --max-time 2 http://127.0.0.1/info; then echo; exit 0; fi
            sleep 1
        done
        exit 1 ;;
    reboot|poweroff) systemctl "$operation" ;;
esac
