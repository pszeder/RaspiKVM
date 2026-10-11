[CmdletBinding(DefaultParameterSetName='Update')]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]*$')][string]$Target,
    [Parameter(Mandatory,ParameterSetName='Update')][string]$Bundle,
    [Parameter(Mandatory,ParameterSetName='Rollback')][switch]$Rollback,
    [ValidatePattern('^[a-z_][a-z0-9_-]*$')][string]$UserName='raspidev',
    [string]$IdentityFile,
    [string]$KnownHostsFile,
    [string]$ExpectedInstanceId,
    [switch]$InteractiveSudo
)
$ErrorActionPreference='Stop'
if ($Target -eq '169.254.1.1') { throw 'Refusing the working RaspiKey address.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$KnownHostsFile) {
    New-Item -ItemType Directory -Force -Path (Join-Path $repo 'build') | Out-Null
    $KnownHostsFile=Join-Path $repo 'build\dev-known-hosts'
}
$sshArgs=@('-o','BatchMode=yes','-o','ConnectTimeout=10','-o','StrictHostKeyChecking=accept-new','-o',"UserKnownHostsFile=$KnownHostsFile")
if ($IdentityFile) { $sshArgs+=@('-i',[IO.Path]::GetFullPath($IdentityFile)) }
$dest="$UserName@$Target"
function Invoke-DevSsh([string]$Command) {
    $result=& ssh @sshArgs $dest $Command
    if ($LASTEXITCODE -ne 0) { throw 'SSH operation failed. Check target, key, USB adapter and ICS.' }
    return $result
}
function Invoke-DevPrivileged([string]$Command) {
    if ($InteractiveSudo) {
        & ssh @sshArgs -t $dest "sudo $Command"
    } else {
        & ssh @sshArgs $dest "sudo -n $Command"
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Development operation failed; see output above. A freshly flashed Pi needs one -InteractiveSudo setup installation.'
    }
}
$probe=Invoke-DevSsh 'hostname; cat /proc/device-tree/model; echo; dpkg --print-architecture; grep ^VERSION_CODENAME= /etc/os-release | cut -d= -f2'
$probeText=($probe -join [string][char]10).Replace([string][char]0,'')
if ($probeText -notmatch '(?m)^raspikey-dev$' -or $probeText -notmatch 'Raspberry Pi Zero W' -or
    $probeText -notmatch '(?m)^armhf$' -or $probeText -notmatch '(?m)^trixie$') {
    throw 'Refusing: SSH target is not raspikey-dev / Pi Zero W / 32-bit Trixie.'
}
if ($ExpectedInstanceId) {
    $actualId=(Invoke-DevSsh 'cat /etc/raspikey-dev-id').Trim()
    if ($actualId -ne $ExpectedInstanceId) { throw 'Refusing: development Pi instance identity changed.' }
}
if ($Rollback) {
    if ($InteractiveSudo) { Invoke-DevPrivileged 'bash /usr/local/libexec/raspikey/rollback-dev.sh' }
    else { Invoke-DevPrivileged '/usr/local/libexec/raspikey/deploy-control.sh rollback' }
    return
}
if (!$InteractiveSudo) {
    # A crashed application must still be repairable with a new deployment.
    [void](Invoke-DevSsh 'test -x /usr/local/libexec/raspikey/deploy-control.sh && sudo -n -l /usr/local/libexec/raspikey/deploy-control.sh')
}
$archive=[IO.Path]::GetFullPath($Bundle)
$sidecar="$archive.sha256"
if (!(Test-Path -LiteralPath $sidecar)) { throw 'Bundle SHA256 sidecar is missing.' }
$expected=([IO.File]::ReadAllText($sidecar) -split '\s+')[0].ToLowerInvariant()
$hashAlgorithm=[Security.Cryptography.SHA256]::Create()
$archiveStream=[IO.File]::OpenRead($archive)
try {
    $actualHash=[BitConverter]::ToString($hashAlgorithm.ComputeHash($archiveStream)).Replace('-','').ToLowerInvariant()
} finally {
    $archiveStream.Dispose()
    $hashAlgorithm.Dispose()
}
if ($expected -notmatch '^[0-9a-f]{64}$' -or $actualHash -ne $expected) {
    throw 'Bundle checksum mismatch.'
}
$remoteDir=(Invoke-DevSsh 'mktemp -d /tmp/raspikey-upload.XXXXXXXX').Trim()
if ($remoteDir -notmatch '^/tmp/raspikey-upload\.[A-Za-z0-9]{8}$') { throw 'Unexpected upload directory.' }
$remoteArchive="$remoteDir/bundle.tar.gz"
$extract=@'
import hashlib, pathlib, shutil, sys, tarfile
archive=pathlib.Path(sys.argv[1])
if hashlib.sha256(archive.read_bytes()).hexdigest() != sys.argv[2]:
    raise SystemExit("Uploaded archive checksum mismatch")
root=archive.parent / "source"
root.mkdir()
try:
    with tarfile.open(archive, "r:gz") as tf:
        for member in tf.getmembers():
            rel=pathlib.PurePosixPath(member.name)
            if rel.is_absolute() or ".." in rel.parts or not (member.isdir() or member.isfile()):
                raise SystemExit("Unsafe archive member")
            target=(root / str(rel)).resolve()
            if not target.is_relative_to(root.resolve()):
                raise SystemExit("Archive member escapes source directory")
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with tf.extractfile(member) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src,dst)
except BaseException:
    shutil.rmtree(root)
    raise
'@
$cleanup=@'
import pathlib,re,shutil,sys
p=pathlib.Path(sys.argv[1])
if p.parent != pathlib.Path("/tmp") or not re.fullmatch(r"raspikey-upload\.[A-Za-z0-9]{8}",p.name) or p.is_symlink():
    raise SystemExit("Unexpected cleanup directory")
if p.exists():
    shutil.rmtree(p)
'@
try {
    & scp @sshArgs $archive ("{0}:{1}" -f $dest,$remoteArchive)
    if ($LASTEXITCODE -ne 0) { throw 'Upload failed.' }
    if ($InteractiveSudo) {
        $extract | & ssh @sshArgs $dest "python3 - '$remoteArchive' '$expected'"
        if ($LASTEXITCODE -ne 0) { throw 'Bundle extraction failed.' }
        Invoke-DevPrivileged "bash '$remoteDir/source/scripts/development/install-dev.sh'"
    } else {
        Invoke-DevPrivileged "/usr/local/libexec/raspikey/deploy-control.sh install '$remoteArchive' '$expected'"
    }
} finally {
    $cleanup | & ssh @sshArgs $dest "python3 - '$remoteDir'"
    if ($LASTEXITCODE -ne 0) { Write-Warning "Temporary upload cleanup failed: $remoteDir" }
}
