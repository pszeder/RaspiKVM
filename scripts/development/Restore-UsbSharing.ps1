# Used by the administrator-owned management task and explicit maintenance.
function Restore-UsbSharing {
    param($Profile,$Adapter,[switch]$Rebind)
    $ErrorActionPreference='Stop'
    if($Profile.HostMac -notmatch '^02-77-' -or $Adapter.MacAddress -ne $Profile.HostMac -or
       $Profile.InstanceId -notmatch '^[a-f0-9-]{36}$'){throw 'Unexpected development adapter identity'}
    $parent=(Get-PnpDeviceProperty -InstanceId $Adapter.PnPDeviceID -KeyName DEVPKEY_Device_Parent -ErrorAction Stop).Data
    if(-not $parent.EndsWith('\'+$Profile.InstanceId,[StringComparison]::OrdinalIgnoreCase)){throw 'Development USB identity differs'}
    $upstreamGuid=[guid]$Profile.SharingPublicGuid
    if($upstreamGuid -eq [guid]::Empty -or $upstreamGuid -eq [guid]$Adapter.InterfaceGuid){throw 'Invalid saved sharing upstream'}
    Start-Service NlaSvc -ErrorAction Stop
    Start-Service SharedAccess -ErrorAction Stop
    $manager=New-Object -ComObject HNetCfg.HNetShare
    $entries=@(foreach($connection in $manager.EnumEveryConnection){
        $p=$manager.NetConnectionProps($connection)
        [pscustomobject]@{Guid=[guid]$p.Guid;Config=$manager.INetSharingConfigurationForINetConnection($connection)}
    })
    $public=@($entries | Where-Object {$_.Config.SharingEnabled -and $_.Config.SharingConnectionType -eq 0})
    $private=@($entries | Where-Object {$_.Config.SharingEnabled -and $_.Config.SharingConnectionType -eq 1})
    if(@($public | Where-Object Guid -ne $upstreamGuid).Count -or
       @($private | Where-Object Guid -ne ([guid]$Adapter.InterfaceGuid)).Count){
        throw 'Sharing has been assigned to another adapter; refusing to redirect it'
    }
    $source=@($entries | Where-Object Guid -eq $upstreamGuid)
    $target=@($entries | Where-Object Guid -eq ([guid]$Adapter.InterfaceGuid))
    if($source.Count -ne 1 -or $target.Count -ne 1){throw 'Saved sharing adapters are unavailable'}
    # Arrival/startup only restores missing roles. A deliberate maintenance
    # rebind can rebuild stale NAT state even when both role flags are set.
    $warnings=@()
    $rebindError=$null
    if($Rebind){
        try {
            if($target[0].Config.SharingEnabled){$target[0].Config.DisableSharing()}
            if($source[0].Config.SharingEnabled){$source[0].Config.DisableSharing()}
        } catch {$rebindError=$_.Exception.Message}
    }
    # Restore missing roles even after a partial rebind failure. Never restart
    # SharedAccess/HNS or select another upstream.
    if(-not $source[0].Config.SharingEnabled){
        try{$source[0].Config.EnableSharing(0)}catch{$warnings+=$_.Exception.Message}
    }
    # Fixed Pi addresses do not need the ICS DHCP listener. Re-enabling a
    # healthy private role can reset host aliases and interrupt active SSH.
    if(-not $target[0].Config.SharingEnabled){
        try{$target[0].Config.EnableSharing(1)}catch{$warnings+=$_.Exception.Message}
    }
    if($rebindError){throw "Sharing rebind failed; attempted to restore the saved roles: $rebindError"}
    $gateway=@(Get-NetIPAddress -InterfaceIndex $Adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object IPAddress -eq '192.168.137.1')
    $dhcp=@(Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue | Where-Object LocalAddress -eq '192.168.137.1')
    [ordered]@{At=(Get-Date).ToString('o');PublicGuid=[string]$upstreamGuid;PrivateGuid=[string]$Adapter.InterfaceGuid;
        GatewayPresent=[bool]$gateway.Count;DhcpListening=[bool]$dhcp.Count;Warnings=$warnings;
        Rebound=[bool]$Rebind;PublicEnabled=[bool]$source[0].Config.SharingEnabled;PrivateEnabled=[bool]$target[0].Config.SharingEnabled;
        Note='Internet routing must also pass a probe from the Pi; role flags are insufficient.'}
}
