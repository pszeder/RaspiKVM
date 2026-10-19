$ErrorActionPreference='Stop'
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('raspikvm-network-test-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\..\scripts\development\Ensure-UsbManagement.ps1') -Destination $testRoot
@{HostMac='02-77-E8-46-AD-01';InstanceId='03368fab-b75b-4537-97d6-fdc8bd891343';ManagementAddress='10.12.194.6'} | ConvertTo-Json | Set-Content (Join-Path $testRoot 'development.json')
$global:RaspiKvmNetTest_changes=New-Object Collections.Generic.List[string]
$global:RaspiKvmNetTest_wrongIdentity=$false
$global:RaspiKvmNetTest_active=$true
$global:RaspiKvmNetTest_persistent=$true
function Get-NetAdapter { [pscustomobject]@{MacAddress='42-63-65-35-18-23';Status='Up';ifIndex=18;PnPDeviceID='production'};[pscustomobject]@{MacAddress='02-77-E8-46-AD-01';Status='Up';ifIndex=32;Name='Development';PnPDeviceID='development'} }
function Get-PnpDeviceProperty { param($InstanceId,$KeyName,$ErrorAction);if($InstanceId -ne 'development'){throw 'Production adapter accessed'};[pscustomobject]@{Data=$(if($global:RaspiKvmNetTest_wrongIdentity){'USB\different-device'}else{'USB\VID_1D6B&PID_0104\03368fab-b75b-4537-97d6-fdc8bd891343'})} }
function Get-NetIPAddress { param($InterfaceIndex,$AddressFamily,$PolicyStore,$ErrorAction);if($InterfaceIndex -ne 32){throw 'Production address queried'};if(($PolicyStore -eq 'ActiveStore' -and $global:RaspiKvmNetTest_active) -or ($PolicyStore -eq 'PersistentStore' -and $global:RaspiKvmNetTest_persistent)){[pscustomobject]@{IPAddress='10.12.194.6';PrefixLength=28}};[pscustomobject]@{IPAddress='192.168.137.1';PrefixLength=24} }
function New-NetIPAddress { param($InterfaceIndex,$IPAddress,$PrefixLength,$SkipAsSource,$PolicyStore);if($InterfaceIndex -ne 32 -or $IPAddress -ne '10.12.194.6' -or $PrefixLength -ne 28){throw 'Unexpected address mutation'};$global:RaspiKvmNetTest_changes.Add('add:'+$PolicyStore) }
function Remove-NetIPAddress { param($InterfaceIndex,$IPAddress,$PolicyStore,[switch]$Confirm);if($InterfaceIndex -ne 32 -or $IPAddress -ne '10.12.194.6' -or $PolicyStore -ne 'ActiveStore'){throw 'Unexpected address removal'};$global:RaspiKvmNetTest_changes.Add('remove:active') }
function Check([bool]$condition,[string]$message){if(!$condition){throw $message}}
& (Join-Path $testRoot 'Ensure-UsbManagement.ps1') -Once
Check ($global:RaspiKvmNetTest_changes.Count -eq 0) 'An existing valid configuration was modified'
$global:RaspiKvmNetTest_persistent=$false
& (Join-Path $testRoot 'Ensure-UsbManagement.ps1') -Once
Check (($global:RaspiKvmNetTest_changes -join ',') -eq 'remove:active,add:') 'Initial persistence was not created using both stores'
$global:RaspiKvmNetTest_changes.Clear();$global:RaspiKvmNetTest_persistent=$true;$global:RaspiKvmNetTest_active=$false
& (Join-Path $testRoot 'Ensure-UsbManagement.ps1') -Once
Check (($global:RaspiKvmNetTest_changes -join ',') -eq 'add:ActiveStore') 'Missing runtime address did not recover'
$global:RaspiKvmNetTest_changes.Clear();$global:RaspiKvmNetTest_wrongIdentity=$true
$rejected=$false
try { & (Join-Path $testRoot 'Ensure-UsbManagement.ps1') -Once }catch{$rejected=$true}
Check ($rejected -and $global:RaspiKvmNetTest_changes.Count -eq 0) 'Different USB identity was not rejected'
Write-Output 'Network guard, idempotence, persistence, and runtime recovery checks passed'
