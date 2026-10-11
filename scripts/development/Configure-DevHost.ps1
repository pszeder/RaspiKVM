[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]*$')][string]$Target,
    [Parameter(Mandatory)][string]$IdentityFile,
    [Parameter(Mandatory)][string]$KnownHostsFile,
    [Parameter(Mandatory)][ValidatePattern('^([0-9A-Fa-f]{2}-){5}[0-9A-Fa-f]{2}$')][string]$HostMac
)
$ErrorActionPreference='Stop'
if($Target -eq '169.254.1.1' -or $HostMac -eq '42-63-65-35-18-23'){throw 'Refusing the original Pi.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$IdentityFile=[IO.Path]::GetFullPath($IdentityFile)
$KnownHostsFile=[IO.Path]::GetFullPath($KnownHostsFile)
$probe=@'
import json,pathlib,subprocess
print(json.dumps({'hostname':subprocess.check_output(['hostname'],text=True).strip(),
'model':pathlib.Path('/proc/device-tree/model').read_text().strip('\0'),
'arch':subprocess.check_output(['dpkg','--print-architecture'],text=True).strip(),
'instanceId':pathlib.Path('/etc/raspikey-dev-id').read_text().strip(),
'deviceMac':pathlib.Path('/sys/class/net/usb0/address').read_text().strip().upper().replace(':','-')}))
'@
$result=$probe | & ssh -i $IdentityFile -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=10 -o "UserKnownHostsFile=$KnownHostsFile" "raspidev@$Target" 'python3 -'
if($LASTEXITCODE -ne 0){throw 'Verify the Pi SSH host key first.'}
$device=($result -join "`n") | ConvertFrom-Json
if($device.hostname -ne 'raspikey-dev' -or $device.model -notmatch 'Raspberry Pi Zero W' -or $device.arch -ne 'armhf' -or $device.instanceId -notmatch '^[0-9a-f-]{36}$'){throw 'Refusing unexpected development Pi.'}
$matchedHostLines=& ssh-keygen -F $Target -f $KnownHostsFile
$hostLines=@($matchedHostLines | Where-Object {$_ -notmatch '^#' -and $_ -match '\s+ssh-ed25519\s+'})
if($hostLines.Count -ne 1){throw 'Expected exactly one verified Ed25519 host key.'}
$fields=$hostLines[0] -split '\s+'
$devProfileDirectory=Join-Path $repo '.dev'
$sshDirectory=Join-Path $devProfileDirectory 'ssh'
New-Item -ItemType Directory -Force -Path $sshDirectory | Out-Null
$privateKey=Join-Path $sshDirectory 'id_ed25519'
if(Test-Path -LiteralPath $privateKey){
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($privateKey)) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes($IdentityFile))){throw 'Refusing to replace a different existing development key.'}
} else {Copy-Item -LiteralPath $IdentityFile -Destination $privateKey}
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
& icacls $privateKey /inheritance:r /grant:r "*${sid}:(R)" | Out-Null
if($LASTEXITCODE){throw 'SSH key permissions could not be set.'}
$publicKey=& ssh-keygen -y -f $privateKey
if($LASTEXITCODE){throw 'Cannot derive the configured public key.'}
[IO.File]::WriteAllText((Join-Path $sshDirectory 'id_ed25519.pub'),($publicKey -join '')+[Environment]::NewLine)
$known=Join-Path $devProfileDirectory 'known_hosts'
if([IO.Path]::GetFullPath($KnownHostsFile) -ne [IO.Path]::GetFullPath($known)){Copy-Item -LiteralPath $KnownHostsFile -Destination $known}
$settings=[ordered]@{Schema=1;Target=$Target;UserName='raspidev';InstanceId=$device.instanceId;HostMac=$HostMac.ToUpperInvariant();DeviceMac=$device.deviceMac;HostKey=($fields[1]+' '+$fields[2]);IdentityFile='.dev/ssh/id_ed25519';KnownHostsFile='.dev/known_hosts'}
$settings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $devProfileDirectory 'settings.json') -Encoding UTF8
Write-Host "Configured local development profile for $Target. Run .\dev.ps1 doctor after permission setup."
