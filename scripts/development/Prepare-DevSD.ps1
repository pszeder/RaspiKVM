[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z]:\\?$')][string]$BootDrive)
$ErrorActionPreference='Stop'
function Enable-RaspiKeyUsbConfig([string]$Text) {
    $lf=[string][char]10
    $text=$Text.Replace(([string][char]13+[char]10),$lf).TrimEnd()+$lf
    if ($text -notmatch '(?m)^#cloud-config[ \t]*$') { throw 'Unrecognized user-data; no changes made.' }
    if ($text -notmatch '(?m)^\s*(?:-\s*)?name:\s*[''"]?raspidev[''"]?[ \t]*$') { throw 'Set the Imager username to raspidev.' }
    if ($text -notmatch '(?m)^hostname:\s*[''"]?raspikey-dev[''"]?[ \t]*$') { throw 'Set the Imager hostname to raspikey-dev.' }
    if ([regex]::Matches($text,'(?m)^rpi:').Count -gt 1) { throw 'Duplicate rpi YAML mappings; no changes made.' }
    if ($text -match '(?m)^rpi:[ \t]*\n') {
        $match=[regex]::Match($text,'(?ms)^rpi:[ \t]*\n(?<body>.*?)(?=^[A-Za-z_][A-Za-z0-9_-]*:|\z)')
        $body=$match.Groups['body'].Value
        $indented=[regex]::Matches($body,'(?m)^( +)[A-Za-z_][A-Za-z0-9_-]*:')
        $indent='  '
        if ($indented.Count -gt 0) {
            $minimum=($indented | ForEach-Object { $_.Groups[1].Length } | Measure-Object -Minimum).Minimum
            $indent=' ' * $minimum
        }
        $pattern='(?m)^'+[regex]::Escape($indent)+'enable_usb_gadget:.*$'
        if ([regex]::Matches($body,$pattern).Count -gt 1) { throw 'Duplicate enable_usb_gadget settings.' }
        if ($body -match $pattern) { $body=[regex]::Replace($body,$pattern,$indent+'enable_usb_gadget: true') }
        else { $body=$indent+'enable_usb_gadget: true'+$lf+$body }
        $text=$text.Substring(0,$match.Index)+'rpi:'+$lf+$body+$text.Substring($match.Index+$match.Length)
    } elseif ($text -match '(?m)^rpi:') { throw 'Unsupported inline rpi mapping; no changes made.' }
    else { $text+='rpi:'+$lf+'  enable_usb_gadget: true'+$lf }
    if ([regex]::Matches($text,'(?m)^enable_ssh:').Count -gt 1) { throw 'Duplicate enable_ssh settings.' }
    if ($text -match '(?m)^enable_ssh:') { $text=[regex]::Replace($text,'(?m)^enable_ssh:.*$','enable_ssh: true') }
    else { $text+='enable_ssh: true'+$lf }
    return $text
}
$root=[IO.Path]::GetFullPath($BootDrive.TrimEnd('\')+'\')
$drive=[IO.DriveInfo]::new($root)
if ($drive.DriveFormat -notin @('FAT','FAT32')) { throw 'Select the FAT bootfs partition of the freshly flashed spare SD card.' }
foreach ($file in @('config.txt','cmdline.txt','user-data')) {
    if (!(Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)) { throw "Missing $file. Use a current Trixie image and current Raspberry Pi Imager." }
}
$path=Join-Path $root 'user-data'
$text=Enable-RaspiKeyUsbConfig ([IO.File]::ReadAllText($path))
if (!(Test-Path -LiteralPath "$path.raspikey-backup")) { [IO.File]::Copy($path,"$path.raspikey-backup") }
[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
Write-Host 'Spare SD prepared for SSH over USB. No disks were formatted.'
Write-Host 'Eject it, insert it in the spare Pi, and use its USB data port. Allow first boot several minutes.'
