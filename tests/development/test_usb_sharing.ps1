$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\..\scripts\development\Restore-UsbSharing.ps1')
$script:Calls=@()
$script:WrongIdentity=$false
$script:Dhcp=$true
$upstream='8d2e788d-3b68-4631-8eac-1d71c5688755'
$private='e8171f13-2fd1-4b4f-b04a-52e82d638bb7'
$other='9f5bb63b-c5e6-421f-b252-a2daacf7da79'
function New-Configuration($enabled,$role,$id){
    $value=[pscustomobject]@{SharingEnabled=$enabled;SharingConnectionType=$role;Id=$id}
    $value | Add-Member ScriptMethod EnableSharing {param($role);$script:Calls+=($this.Id+':'+$role);$this.SharingEnabled=$true;$this.SharingConnectionType=$role}
    return $value
}
$source=New-Configuration $true 0 $upstream
$target=New-Configuration $true 1 $private
$production=New-Configuration $false 1 $other
$script:Entries=@(
    [pscustomobject]@{Guid=$upstream;Config=$source},
    [pscustomobject]@{Guid=$private;Config=$target},
    [pscustomobject]@{Guid=$other;Config=$production})
$script:Manager=[pscustomobject]@{EnumEveryConnection=$script:Entries}
$script:Manager | Add-Member ScriptMethod NetConnectionProps {param($c);$c}
$script:Manager | Add-Member ScriptMethod INetSharingConfigurationForINetConnection {param($c);$c.Config}
function New-Object {param($ComObject);if($ComObject -ne 'HNetCfg.HNetShare'){throw 'Unexpected COM access'};$script:Manager}
function Start-Service {param($Name,$ErrorAction);if($Name -notin @('NlaSvc','SharedAccess')){throw 'Unexpected service change'}}
function Get-PnpDeviceProperty {param($InstanceId,$KeyName,$ErrorAction);[pscustomobject]@{Data=$(if($script:WrongIdentity){'USB\wrong'}else{'USB\03368fab-b75b-4537-97d6-fdc8bd891343'})}}
function Get-NetUDPEndpoint {param($LocalPort,$ErrorAction);if($script:Dhcp){[pscustomobject]@{LocalAddress='192.168.137.1'}}}
function Get-NetIPAddress {param($InterfaceIndex,$AddressFamily,$ErrorAction);if($InterfaceIndex -ne 32){throw 'Production address accessed'};[pscustomobject]@{IPAddress='192.168.137.1'}}
function Check($condition,$message){if(!$condition){throw $message}}
$profile=[pscustomobject]@{HostMac='02-77-E8-46-AD-01';InstanceId='03368fab-b75b-4537-97d6-fdc8bd891343';SharingPublicGuid=$upstream}
$adapter=[pscustomobject]@{MacAddress=$profile.HostMac;PnPDeviceID='development';InterfaceGuid=$private;ifIndex=32}
Restore-UsbSharing $profile $adapter | Out-Null
Check ($script:Calls.Count -eq 0) 'Healthy sharing changed'
$target.SharingEnabled=$false
Restore-UsbSharing $profile $adapter | Out-Null
Check (($script:Calls -join ',') -eq ($private+':1')) 'Did not restore only the saved private adapter'
$script:Calls=@();$production.SharingEnabled=$true
$rejected=$false;try{Restore-UsbSharing $profile $adapter | Out-Null}catch{$rejected=$true}
Check ($rejected -and !$script:Calls.Count) 'Redirected another private connection'
$production.SharingEnabled=$false;$script:WrongIdentity=$true
$rejected=$false;try{Restore-UsbSharing $profile $adapter | Out-Null}catch{$rejected=$true}
Check ($rejected -and !$script:Calls.Count) 'Accepted wrong USB identity'
$script:WrongIdentity=$false;$source.SharingEnabled=$false;$target.SharingEnabled=$false
Restore-UsbSharing $profile $adapter | Out-Null
Check (($script:Calls -join ',') -eq ($upstream+':0,'+$private+':1')) 'Did not restore exactly the saved pair'
'PASS: healthy sharing unchanged, saved pair restored, other private adapter and wrong USB identity refused'
