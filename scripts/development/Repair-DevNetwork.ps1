[CmdletBinding()]
param([string]$ConfigFile,[switch]$Persist)
$ErrorActionPreference='Stop'
if([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA'){throw 'Run with Windows PowerShell 5.1 -STA.'}
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this network maintenance command as Administrator.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(!$ConfigFile){$ConfigFile=Join-Path $repo '.dev\settings.json'}
$profile=Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
if($profile.HostMac -eq '42-63-65-35-18-23' -or $profile.HostMac -notmatch '^02-77-'){throw 'Refusing an unexpected or production adapter.'}
$dev=@(Get-NetAdapter | Where-Object MacAddress -eq $profile.HostMac)
if($dev.Count -ne 1 -or $dev[0].Status -ne 'Up'){throw 'Connect the configured development Pi first.'}
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
# Starting NLA restored ICS notifications/DHCP on the Windows 11 development host.
# Do not restart SharedAccess or touch unrelated adapters and Hyper-V networks.
Start-Service NlaSvc
Start-Service SharedAccess
$warning=$null
try{$target[0].Config.EnableSharing(1)}catch{$warning=$_.Exception.Message}
if(-not $target[0].Config.SharingEnabled -or $target[0].Config.SharingConnectionType -ne 1){throw "Development sharing is not active. $warning"}
$deadline=(Get-Date).AddSeconds(45)
do{
    $address=@(Get-NetIPAddress -InterfaceIndex $dev[0].ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object IPAddress -eq '192.168.137.1')
    $dhcp=@(Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue | Where-Object LocalAddress -eq '192.168.137.1')
    if($address.Count -and $dhcp.Count){break}
    Start-Sleep -Seconds 2
}while((Get-Date) -lt $deadline)
if(-not $address.Count -or -not $dhcp.Count){throw 'ICS roles are configured but DHCP is not listening on 192.168.137.1. Do not treat role flags alone as recovery.'}
[pscustomobject]@{Public=$public[0].Name;Private=$dev[0].Name;HostMac=$profile.HostMac;Address='192.168.137.1';DhcpListening=$true;Persistence=[bool]$Persist;NotificationWarning=$warning}
