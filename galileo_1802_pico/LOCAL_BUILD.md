# Local UF2 Build Quick Start

This folder is the complete Galileo CDP1802 Pico source tree. Historical Galileo ROM/RAM bytes are not included in the UF2.

## Windows PowerShell

1. Install Arduino CLI and ensure `arduino-cli` is in PATH.
2. Open PowerShell in this folder.
3. If script execution is blocked, run:

   `Set-ExecutionPolicy -Scope Process Bypass`

4. Run:

   `.\build_uf2_windows.ps1`

5. The UF2 will be in:

   `build\galileo_1802_pico\`

## macOS / Linux

Install Arduino CLI. Homebrew users may run:

`brew install arduino-cli`

Then, in this folder:

`./build_uf2_macos_linux.sh`

The UF2 will be in:

`build/galileo_1802_pico/`

## Exact reference configuration

- Arduino-Pico core: 6.0.0
- Board: Raspberry Pi Pico / RP2040
- Flash layout: 2 MB with 64 KB LittleFS
- USB stack: Pico SDK USB CDC
- Optimization: -O2

Exact FQBN:

`rp2040:rp2040:rpipico:flash=2097152_65536,usbstack=picosdk,opt=Optimize2`

## After flashing

The UF2 intentionally contains no historical flight image. Install your local `rom image` and `ram image` with:

`python -m pip install pyserial`

Windows example:

`python tools/load_galileo_images.py --port COM7 --rom "rom image" --ram "ram image"`

macOS/Linux example:

`python3 tools/load_galileo_images.py --port /dev/ttyACM0 --rom "rom image" --ram "ram image"`
