[CmdletBinding()]
param([string]$ConfigFile)
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
[ordered]@{InstanceId=$profile.InstanceId;HostMac=$profile.HostMac;ManagementAddress='10.12.194.6'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'development.json')
$script=Join-Path $destination 'Ensure-UsbManagement.ps1'
$action=New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+$script+'"')
$triggers=@((New-ScheduledTaskTrigger -AtStartup),(New-ScheduledTaskTrigger -AtLogOn))
$settings=New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 3)
$principal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName 'RaspiKVM USB management' -Action $action -Trigger $triggers -Settings $settings -Principal $principal -Description 'Restore the fixed management address on the identified development Pi USB adapter; preserve Internet Sharing and all other adapters.' -Force | Out-Null
& $script -Once
Get-Content -LiteralPath (Join-Path $destination 'last-result.json')
