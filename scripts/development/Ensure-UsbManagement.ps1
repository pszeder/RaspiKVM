[CmdletBinding()]
param([string]$ConfigFile=(Join-Path $PSScriptRoot 'development.json'),[switch]$Once)
$ErrorActionPreference='Stop'
$profile=Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
if($profile.HostMac -notmatch '^02-77-' -or $profile.InstanceId -notmatch '^[a-f0-9-]{36}$' -or $profile.ManagementAddress -ne '10.12.194.6'){throw 'Unexpected development network profile'}
$deadline=(Get-Date).AddSeconds($(if($Once){5}else{120}))
do {
    try {
        $dev=@(Get-NetAdapter -ErrorAction Stop | Where-Object MacAddress -eq $profile.HostMac)
        if($dev.Count -eq 1 -and $dev[0].Status -eq 'Up') {
            $parent=(Get-PnpDeviceProperty -InstanceId $dev[0].PnPDeviceID -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data
            if(-not $parent.EndsWith('\'+$profile.InstanceId,[StringComparison]::OrdinalIgnoreCase)){throw 'Development USB identity differs'}
            $active=@(Get-NetIPAddress -InterfaceIndex $dev[0].ifIndex -AddressFamily IPv4 -PolicyStore ActiveStore -ErrorAction SilentlyContinue | Where-Object IPAddress -eq $profile.ManagementAddress)
            $persistent=@(Get-NetIPAddress -InterfaceIndex $dev[0].ifIndex -AddressFamily IPv4 -PolicyStore PersistentStore -ErrorAction SilentlyContinue | Where-Object IPAddress -eq $profile.ManagementAddress)
            if(@($active+$persistent | Where-Object PrefixLength -ne 28).Count){throw 'Existing management address has a different prefix'}
            if(!$persistent.Count) {
                # Omitting PolicyStore creates both stores. Explicit PersistentStore
                # creation is rejected by NetTCPIP on this Windows version.
                if($active.Count){Remove-NetIPAddress -InterfaceIndex $dev[0].ifIndex -IPAddress $profile.ManagementAddress -PolicyStore ActiveStore -Confirm:$false}
                New-NetIPAddress -InterfaceIndex $dev[0].ifIndex -IPAddress $profile.ManagementAddress -PrefixLength 28 -SkipAsSource $false | Out-Null
            } elseif(!$active.Count) {
                New-NetIPAddress -InterfaceIndex $dev[0].ifIndex -IPAddress $profile.ManagementAddress -PrefixLength 28 -SkipAsSource $false -PolicyStore ActiveStore | Out-Null
            }
            [ordered]@{At=(Get-Date).ToString('o');Success=$true;Adapter=$dev[0].Name;HostMac=$profile.HostMac;Address=$profile.ManagementAddress;Pi='10.12.194.1'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'last-result.json')
        } elseif($Once) {throw 'Development USB adapter is not connected'}
    } catch {
        [ordered]@{At=(Get-Date).ToString('o');Success=$false;Error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'last-result.json')
        if($Once){throw}
    }
    if($Once){break}
    Start-Sleep -Seconds 3
} while((Get-Date) -lt $deadline)
