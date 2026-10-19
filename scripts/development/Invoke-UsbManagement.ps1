$ErrorActionPreference='Stop'
try {
    & (Join-Path $PSScriptRoot 'Ensure-UsbManagement.ps1') -ConfigFile (Join-Path $PSScriptRoot 'development.json')
} catch {
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'startup-error.txt'),($_ | Out-String))
    exit 1
}
