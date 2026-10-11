[CmdletBinding()]
param([string]$Distribution='Ubuntu',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$OutputDirectory) { $OutputDirectory=Join-Path $repo 'dist' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$srcResult=& wsl -d $Distribution --exec wslpath -u ($repo.Replace('\','/'))
if ($LASTEXITCODE) { throw 'WSL path conversion failed.' }
$src=($srcResult -join '').Trim()
$outResult=& wsl -d $Distribution --exec wslpath -u ([IO.Path]::GetFullPath($OutputDirectory).Replace('\','/'))
if ($LASTEXITCODE) { throw 'WSL path conversion failed.' }
$out=($outResult -join '').Trim()
& wsl -d $Distribution --exec bash "$src/scripts/development/build-wsl.sh" $src $out
if ($LASTEXITCODE) { throw 'WSL frontend build failed.' }
