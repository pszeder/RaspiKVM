#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
. "$here/assert-dev.sh"
[[ -f /etc/raspikey-dev-id && -x "$here/deploy-control.sh" ]] || exit 1
python3 -I - <<'PY'
import json, os, pathlib, pwd
config={'schema':1,'user':'raspidev','uid':pwd.getpwnam('raspidev').pw_uid,
        'instanceId':pathlib.Path('/etc/raspikey-dev-id').read_text().strip()}
framework=pathlib.Path('/usr/local/libexec/raspikey/framework-manifest.json')
if framework.exists(): config['frameworkRelease']=json.loads(framework.read_text())['release']
target=pathlib.Path('/etc/raspikey-dev-deploy.json')
temporary=target.with_suffix('.next')
temporary.write_text(json.dumps(config,indent=2)+'\n')
temporary.chmod(0o644)
os.replace(temporary,target)
PY
rule="$(mktemp /etc/sudoers.d/.raspikey-deploy.XXXXXXXX)"
trap 'rm -f -- "$rule"' EXIT
printf 'raspidev ALL=(root) NOPASSWD: /usr/local/libexec/raspikey/deploy-control.sh\n' > "$rule"
chmod 440 "$rule"
visudo -cf "$rule"
mv -Tf "$rule" /etc/sudoers.d/raspikey-dev-deploy
echo 'SSH-key-only deployment configured for raspidev.'
