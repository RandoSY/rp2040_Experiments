$ErrorActionPreference = "Stop"

Write-Host "Galileo 1802 Pico UF2 build"
Write-Host "==========================="

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw "arduino-cli is not in PATH. Install Arduino CLI first, reopen PowerShell, and try again."
}

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir
$IndexUrl = "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

Write-Host "`n[1/4] Arduino CLI version"
arduino-cli version

Write-Host "`n[2/4] Refresh Arduino-Pico package index"
arduino-cli core update-index --additional-urls $IndexUrl

Write-Host "`n[3/4] Install pinned Arduino-Pico core 6.0.0"
arduino-cli core install "rp2040:rp2040@6.0.0" --additional-urls $IndexUrl

Write-Host "`n[4/4] Compile Raspberry Pi Pico UF2"
$BuildDir = Join-Path $ProjectDir "build\galileo_1802_pico"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

arduino-cli compile `
  --fqbn "rp2040:rp2040:rpipico:flash=2097152_65536,usbstack=picosdk,opt=Optimize2" `
  --warnings all `
  --output-dir $BuildDir `
  $ProjectDir

Write-Host "`nBuild complete. UF2 file(s):"
Get-ChildItem $BuildDir -Filter *.uf2 | Format-Table Name,Length,FullName -AutoSize
