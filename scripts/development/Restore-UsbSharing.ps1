# Loaded only by the administrator-owned Windows management task.
function Restore-UsbSharing {
    param($Profile,$Adapter)
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
    # Reapply only this saved pair after USB arrival. Never disable a connection,
    # restart SharedAccess/HNS, or choose a different upstream automatically.
    $warnings=@()
    if(-not $source[0].Config.SharingEnabled){
        try{$source[0].Config.EnableSharing(0)}catch{$warnings+=$_.Exception.Message}
    }
    $dhcp=@(Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue | Where-Object LocalAddress -eq '192.168.137.1')
    if(-not $target[0].Config.SharingEnabled -or !$dhcp.Count){
        try{$target[0].Config.EnableSharing(1)}catch{$warnings+=$_.Exception.Message}
    }
    $gateway=@(Get-NetIPAddress -InterfaceIndex $Adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object IPAddress -eq '192.168.137.1')
    $dhcp=@(Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue | Where-Object LocalAddress -eq '192.168.137.1')
    [ordered]@{At=(Get-Date).ToString('o');PublicGuid=[string]$upstreamGuid;PrivateGuid=[string]$Adapter.InterfaceGuid;
        GatewayPresent=[bool]$gateway.Count;DhcpListening=[bool]$dhcp.Count;Warnings=$warnings;
        Note='Internet routing must also pass a probe from the Pi; role flags are insufficient.'}
}
