[CmdletBinding()]
param(
    [Parameter(Position=0)][ValidateSet('doctor','build','deploy','update','setup','rollback','restart','logs','ssh','reboot','poweroff')][string]$Action='doctor',
    [string]$Target,
    [string]$Bundle,
    [string]$ConfigFile
)
$ErrorActionPreference='Stop'
if(!$ConfigFile){$ConfigFile=Join-Path $PSScriptRoot '.dev\settings.json'}
$tools=Join-Path $PSScriptRoot 'scripts\development'
if($Action -eq 'build') { & (Join-Path $tools 'Build-DevBundle.ps1'); return }
if(!(Test-Path -LiteralPath $ConfigFile)) {throw 'Local development profile missing. Follow docs/development.html to configure .dev/settings.json.'}
$config=Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
$key=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $config.IdentityFile))
$known=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $config.KnownHostsFile))
if(!(Test-Path -LiteralPath $key) -or !(Test-Path -LiteralPath $known)){throw 'Configured SSH key or verified known-hosts file is missing.'}
if(!$Target) {
    $adapter=@(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object {$_.MacAddress -eq $config.HostMac})
    if($adapter.Count -eq 1) {
        $ips=@(Get-NetIPAddress -InterfaceIndex $adapter[0].ifIndex -AddressFamily IPv4)
        if($ips.IPAddress -contains '192.168.137.1') {
            $neighbor=@(Get-NetNeighbor -InterfaceIndex $adapter[0].ifIndex -AddressFamily IPv4 | Where-Object {$_.LinkLayerAddress -eq $config.DeviceMac -and $_.IPAddress -like '192.168.137.*'})
            if($neighbor.Count -eq 1){$Target=$neighbor[0].IPAddress}
        } elseif(@($ips | Where-Object {$_.IPAddress -like '10.12.194.*'}).Count) {$Target='10.12.194.1'}
    }
    if(!$Target){$Target=$config.Target}
}
if($Target -eq '169.254.1.1' -or $Target -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]*$'){throw 'Refusing unexpected development target.'}
# DHCP can change an address, but it must still present the pinned Pi host key.
$hosts=[IO.File]::ReadAllLines($known)
if(!@($hosts | Where-Object {$_ -match ('^'+[regex]::Escape($Target)+'\s')}).Count) {
    if(!$config.HostKey -or $config.HostKey -notmatch '^ssh-ed25519 [A-Za-z0-9+/=]+$'){throw 'Pinned host key missing.'}
    [IO.File]::AppendAllText($known,"$Target $($config.HostKey)"+[Environment]::NewLine)
}
$sshArgs=@('-i',$key,'-o','BatchMode=yes','-o','ConnectTimeout=10','-o','StrictHostKeyChecking=yes','-o',"UserKnownHostsFile=$known")
$dest="$($config.UserName)@$Target"
$identity=& ssh @sshArgs $dest 'cat /etc/raspikey-dev-id'
if($LASTEXITCODE -ne 0 -or ($identity -join '').Trim() -ne $config.InstanceId){throw 'Development Pi identity/SSH verification failed.'}
if($Action -eq 'ssh'){& ssh @sshArgs -t $dest; return}
if($Action -in @('deploy','update','setup','rollback')) {
    if($Action -eq 'update'){& (Join-Path $tools 'Build-DevBundle.ps1')}
    $deployArgs=@{Target=$Target;IdentityFile=$key;KnownHostsFile=$known;UserName=$config.UserName;ExpectedInstanceId=$config.InstanceId}
    if($Action -eq 'setup'){$deployArgs.InteractiveSudo=$true}
    if($Action -eq 'rollback') {$deployArgs.Rollback=$true}
    else {
        if(!$Bundle){$Bundle=(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'dist') -Filter 'raspikey-dev-*.tar.gz' | Where-Object {$_.Name -notmatch 'fault-test'} | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName}
        if(!$Bundle){throw 'Build a bundle first: .\dev.ps1 build'}
        $deployArgs.Bundle=$Bundle
    }
    & (Join-Path $tools 'Deploy-DevBundle.ps1') @deployArgs
    return
}
if($Action -eq 'doctor') {
    & ssh @sshArgs $dest 'sudo -n /usr/local/libexec/raspikey/deploy-control.sh status'
    if($LASTEXITCODE -ne 0){throw 'Password-free deployment/service health check failed.'}
    $program=@'
import json, pathlib, subprocess, urllib.request
def run(args): return subprocess.check_output(args,text=True).strip()
devices=json.load(urllib.request.urlopen('http://localhost/bt/devices',timeout=5))
checks={'hostname':run(['hostname']),'model':pathlib.Path('/proc/device-tree/model').read_text().strip('\0'),
'architecture':run(['dpkg','--print-architecture']),'bluetoothPowered': 'Powered: yes' in run(['bluetoothctl','show']),
'pairedKeyboardCount':sum(d.get('icon')=='input-keyboard' and d.get('paired',False) and d.get('connected',False) for d in devices),
'pairedMouseCount':sum(d.get('icon')=='input-mouse' and d.get('paired',False) and d.get('connected',False) for d in devices),
'usbInternet':urllib.request.urlopen('https://deb.debian.org/',timeout=15).status==200}
checks['deploymentFramework']=json.loads(pathlib.Path('/etc/raspikey-dev-deploy.json').read_text()).get('frameworkRelease','setup-1')
print(json.dumps(checks,indent=2))
assert checks['hostname']=='raspikey-dev' and checks['architecture']=='armhf'
assert checks['bluetoothPowered'] and checks['usbInternet']
'@
    $program | & ssh @sshArgs $dest 'python3 -'
    if($LASTEXITCODE -ne 0){throw 'Development readiness checks failed.'}
    Write-Host "Development Pi ready: http://$Target/html/index.html"
    return
}
& ssh @sshArgs $dest "sudo -n /usr/local/libexec/raspikey/deploy-control.sh $Action"
if($LASTEXITCODE -ne 0 -and !($Action -in @('reboot','poweroff') -and $LASTEXITCODE -eq 255)){throw 'Development operation failed.'}
