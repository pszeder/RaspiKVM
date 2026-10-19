[CmdletBinding()]
param([string]$ConfigFile,[switch]$Persist,[switch]$Rebind)
$ErrorActionPreference='Stop'
if([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA'){throw 'Run with Windows PowerShell 5.1 -STA.'}
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this network maintenance command as Administrator.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(!$ConfigFile){$ConfigFile=Join-Path $repo '.dev\settings.json'}
$profile=Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
if($profile.HostMac -eq '42-63-65-35-18-23' -or $profile.HostMac -notmatch '^02-77-'){throw 'Refusing an unexpected or production adapter.'}
$dev=@(Get-NetAdapter | Where-Object MacAddress -eq $profile.HostMac)
if($dev.Count -ne 1 -or $dev[0].Status -ne 'Up'){throw 'Connect the configured development Pi first.'}
$parent=(Get-PnpDeviceProperty -InstanceId $dev[0].PnPDeviceID -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data
if($profile.InstanceId -notmatch '^[a-f0-9-]{36}$' -or -not $parent.EndsWith('\'+$profile.InstanceId,[StringComparison]::OrdinalIgnoreCase)){throw 'Development USB identity differs.'}
$sharing=New-Object -ComObject HNetCfg.HNetShare
$entries=@(foreach($connection in $sharing.EnumEveryConnection){
    $properties=$sharing.NetConnectionProps($connection)
    $configuration=$sharing.INetSharingConfigurationForINetConnection($connection)
    [pscustomobject]@{Name=$properties.Name;Guid=[guid]$properties.Guid;Config=$configuration}
})
$public=@($entries | Where-Object {$_.Config.SharingEnabled -and $_.Config.SharingConnectionType -eq 0})
$private=@($entries | Where-Object {$_.Config.SharingEnabled -and $_.Config.SharingConnectionType -eq 1})
if($public.Count -ne 1 -or $public[0].Guid -eq [guid]$dev[0].InterfaceGuid -or $private.Count -gt 1 -or ($private.Count -eq 1 -and $private[0].Guid -ne [guid]$dev[0].InterfaceGuid)){throw 'Configure the intended upstream-to-development ICS pair first; refusing to redirect another shared connection.'}
$target=@($entries | Where-Object Guid -eq ([guid]$dev[0].InterfaceGuid))
if($target.Count -ne 1){throw 'Development sharing adapter not found.'}
$managementRoot=Join-Path $env:ProgramData 'RaspiKVM\Network'
$managementScript=Join-Path $managementRoot 'Ensure-UsbManagement.ps1'
$managementConfig=Join-Path $managementRoot 'development.json'
$management=$null
if(Test-Path -LiteralPath $managementConfig){
    $management=Get-Content -LiteralPath $managementConfig -Raw | ConvertFrom-Json
    if($management.HostMac -ne $profile.HostMac -or $management.InstanceId -ne $profile.InstanceId -or
       ($management.SharingPublicGuid -and [guid]$management.SharingPublicGuid -ne $public[0].Guid)){throw 'Installed management profile differs; refusing repair.'}
}
if($Rebind -and (!$management -or !(Test-Path -LiteralPath $managementScript))){throw 'Install fixed USB management before rebinding sharing so the control address can be restored.'}
$backup=Join-Path (Split-Path -Parent $ConfigFile) 'network-maintenance-before.json'
if(!(Test-Path -LiteralPath $backup)){
    [ordered]@{At=(Get-Date).ToString('o');PublicGuid=[string]$public[0].Guid;PrivateGuid=[string]$dev[0].InterfaceGuid;
        SharedAccessStart=(Get-CimInstance Win32_Service -Filter "Name='SharedAccess'").StartMode;
        NlaSvcStart=(Get-CimInstance Win32_Service -Filter "Name='NlaSvc'").StartMode;
        RebootPersist=(Get-ItemProperty -Path HKLM:\Software\Microsoft\Windows\CurrentVersion\SharedAccess -Name EnableRebootPersistConnection -ErrorAction SilentlyContinue).EnableRebootPersistConnection} | ConvertTo-Json | Set-Content -LiteralPath $backup
}
if($Persist){
    New-Item -Path HKLM:\Software\Microsoft\Windows\CurrentVersion\SharedAccess -Force | Out-Null
    New-ItemProperty -Path HKLM:\Software\Microsoft\Windows\CurrentVersion\SharedAccess -Name EnableRebootPersistConnection -Value 1 -PropertyType DWord -Force | Out-Null
    Set-Service SharedAccess -StartupType Automatic
    Set-Service NlaSvc -StartupType Automatic
}
. (Join-Path $PSScriptRoot 'Restore-UsbSharing.ps1')
$repairProfile=[pscustomobject]@{HostMac=$profile.HostMac;InstanceId=$profile.InstanceId;SharingPublicGuid=[string]$public[0].Guid}
try {$state=Restore-UsbSharing -Profile $repairProfile -Adapter $dev[0] -Rebind:$Rebind}
finally {
    # ICS can remove secondary host addresses while rebuilding its pair.
    # Restore the independent control address on success and partial failure.
    if($management -and (Test-Path -LiteralPath $managementScript)){& $managementScript -Once}
}
if(!$state.PublicEnabled -or !$state.PrivateEnabled){throw ('Development sharing roles are not active. '+($state.Warnings -join '; '))}
$state['Persistence']=[bool]$Persist
$state['Note']='Validate forwarding from the Pi: .\dev.ps1 doctor -RequireInternet. Fixed addresses do not require a DHCP listener.'
[pscustomobject]$state
