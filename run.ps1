param([switch]$InjectMemoryFault, [switch]$EnableQmp, [switch]$Headless)

$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$DataDisk = Join-Path $Root "build\rawline-data.img"

New-Item -ItemType Directory -Force (Split-Path $DataDisk) | Out-Null
if (!(Test-Path -LiteralPath $DataDisk)) {
    & python (Join-Path $Root "tools\mkfat32.py") $DataDisk
    if ($LASTEXITCODE -ne 0) { throw "FAT32 data disk creation failed." }
}

if ($InjectMemoryFault) { & "$Root\build.ps1" -InjectMemoryFault }
else { & "$Root\build.ps1" }

$Qemu = "D:\qemu\qemu-system-x86_64.exe"
$Uefi = "D:/qemu/share/edk2-x86_64-code.fd"

if (!(Test-Path $Qemu)) {
    throw "QEMU not found: $Qemu"
}

if (!(Test-Path $Uefi)) {
    throw "UEFI firmware not found: $Uefi"
}

Write-Host ""
Write-Host "=== Booting Rawline ==="

Push-Location $Root

try {
    $QemuArgs = @(
        "-machine", "q35",
        "-m", "512M",
        "-drive", "if=pflash,format=raw,readonly=on,file=$Uefi",
        "-drive", "file=fat:rw:build/image,format=raw",
        "-drive", "if=none,id=rawline_data,format=raw,file=$DataDisk,cache=writeback",
        "-device", "virtio-blk-pci,drive=rawline_data,disable-modern=on",
        "-device", "virtio-vga,disable-modern=on",
        "-serial", "stdio"
    )
    if ($Headless) { $QemuArgs += @("-display", "none") }
    if ($EnableQmp) { $QemuArgs += @("-qmp", "tcp:127.0.0.1:4444,server=on,wait=off") }
    & $Qemu @QemuArgs
}
finally {
    Pop-Location
}
