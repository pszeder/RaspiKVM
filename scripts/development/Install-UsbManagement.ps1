[CmdletBinding()]
param([string]$ConfigFile,[switch]$RestoreInternetSharing)
$ErrorActionPreference='Stop'
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this one-time host setup as Administrator'}
if(!$ConfigFile){$ConfigFile=Join-Path $PSScriptRoot '..\..\.dev\settings.json'}
$profile=Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
if($profile.HostMac -notmatch '^02-77-' -or $profile.InstanceId -notmatch '^[a-f0-9-]{36}$'){throw 'Unexpected development identity'}
$destination=Join-Path $env:ProgramData 'RaspiKVM\Network'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
# The SYSTEM startup task must execute only administrator-owned code/configuration.
$acl=New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true,$false)
foreach($sidText in @('S-1-5-18','S-1-5-32-544')) {
    $sid=New-Object Security.Principal.SecurityIdentifier($sidText)
    $rule=New-Object Security.AccessControl.FileSystemAccessRule($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow')
    $acl.AddAccessRule($rule)
}
$readers=New-Object Security.Principal.SecurityIdentifier('S-1-5-32-545')
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($readers,'ReadAndExecute','ContainerInherit,ObjectInherit','None','Allow')))
Set-Acl -LiteralPath $destination -AclObject $acl
if(!(Test-Path -LiteralPath (Join-Path $destination 'before.json'))) {
    $dev=@(Get-NetAdapter | Where-Object MacAddress -eq $profile.HostMac)
    if($dev.Count -ne 1){throw 'Development adapter unavailable'}
    Get-NetIPAddress -InterfaceIndex $dev[0].ifIndex -AddressFamily IPv4 | Select-Object IPAddress,PrefixLength,SkipAsSource | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'before.json')
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Ensure-UsbManagement.ps1') -Destination $destination -Force
$saved=[ordered]@{InstanceId=$profile.InstanceId;HostMac=$profile.HostMac;ManagementAddress='10.12.194.6'}
if($RestoreInternetSharing){
    $sharing=New-Object -ComObject HNetCfg.HNetShare
    $public=@(foreach($connection in $sharing.EnumEveryConnection){
        $config=$sharing.INetSharingConfigurationForINetConnection($connection)
        if($config.SharingEnabled -and $config.SharingConnectionType -eq 0){$sharing.NetConnectionProps($connection)}
    })
    if($public.Count -ne 1){throw 'Configure the intended sharing upstream before saving its identity.'}
    $saved.SharingPublicGuid=[string]$public[0].Guid
    New-Item -Path HKLM:\Software\Microsoft\Windows\CurrentVersion\SharedAccess -Force | Out-Null
    New-ItemProperty -Path HKLM:\Software\Microsoft\Windows\CurrentVersion\SharedAccess -Name EnableRebootPersistConnection -Value 1 -PropertyType DWord -Force | Out-Null
    Set-Service NlaSvc -StartupType Automatic
    Set-Service SharedAccess -StartupType Automatic
} elseif(Test-Path (Join-Path $destination 'development.json')){
    $old=Get-Content (Join-Path $destination 'development.json') -Raw | ConvertFrom-Json
    if($old.InstanceId -eq $profile.InstanceId -and $old.HostMac -eq $profile.HostMac -and $old.SharingPublicGuid){$saved.SharingPublicGuid=$old.SharingPublicGuid}
}
$saved | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'development.json')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Restore-UsbSharing.ps1') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Invoke-UsbManagement.ps1') -Destination $destination -Force
$taskScript=Join-Path $destination 'Invoke-UsbManagement.ps1'
$script=Join-Path $destination 'Ensure-UsbManagement.ps1'
$action=New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -WorkingDirectory $destination -Argument ('-NoProfile -STA -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+$taskScript+'"')
$triggers=@((New-ScheduledTaskTrigger -AtStartup),(New-ScheduledTaskTrigger -AtLogOn))
# A Zero can finish booting after the startup task's bounded retry window.
# Network arrival starts the same task again; its USB UUID/MAC guards still apply.
$eventClass=Get-CimClass -Namespace Root/Microsoft/Windows/TaskScheduler -ClassName MSFT_TaskEventTrigger
$arrival=New-CimInstance -ClientOnly -CimClass $eventClass -Property @{
    Enabled=$true
    Subscription='<QueryList><Query Id="0" Path="Microsoft-Windows-NetworkProfile/Operational"><Select Path="Microsoft-Windows-NetworkProfile/Operational">*[System[(EventID=10000)]]</Select></Query></QueryList>'
}
$triggers+=$arrival
$settings=New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 3)
$principal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName 'RaspiKVM USB management' -Action $action -Trigger $triggers -Settings $settings -Principal $principal -Description 'Restore the fixed management address on the identified development Pi USB adapter; preserve Internet Sharing and all other adapters.' -Force | Out-Null
$connected=@(Get-NetAdapter | Where-Object {$_.MacAddress -eq $profile.HostMac -and $_.Status -eq 'Up'})
if($connected.Count -eq 1){
    & $script -Once
    Get-Content -LiteralPath (Join-Path $destination 'last-result.json')
} else {
    Start-ScheduledTask -TaskName 'RaspiKVM USB management'
    Write-Host 'Protected startup task installed; waiting for development USB network arrival.'
}
