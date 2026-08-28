# Galileo MAG on RP2040 — CDP1802 software resurrection

This project turns a Raspberry Pi Pico/RP2040 into a **software-emulated RCA CDP1802** capable of executing the preserved Galileo magnetometer flight ROM and RAM image.

It is derived conceptually from `wd5gnr/1802black` (RP2040/Arduino 1802 emulator), but the CPU core here has been tightened against the independently tested JavaScript CDP1802 reference used by the Galileo browser lab. In particular this version implements the features the upstream emulator explicitly lacked and the Galileo flight program requires: **IE, true RET/DIS semantics, IDL-as-wait, interrupt entry, and LSIE**.

## What is in the firmware

- complete CDP1802 software CPU core
- 16 registers, P/X/D/DF/Q/T/IE and EF1-EF4
- Galileo memory map
  - `$0000-$0FFF` 4K ROM
  - `$4000-$4FFF` 4K RAM
  - `$7000-$7FFF` instrument hardware/MMIO
- Bank 0/4 swap
- RAM page protection
- documented `$7002/$7003` control/status
- 16-channel 12-bit ADC fixture
- 8x8 hardware multiplier
- control switches and flip interlocks
- archive-derived `$7033 -> RET` ENABLE-INT compatibility behavior
- approximately 1.6 MHz real-time execution model
- 30 Hz RTI/EF1 interrupt profile
- USB CDC terminal
- verified calls into selected preserved Galileo Forth words
- flash-backed storage of the authentic ROM/RAM using LittleFS
- machine-readable CDP1802 state/step protocol for hardware-in-the-loop validation

## Important image policy

The historical Galileo image bytes are **not included** in this repository or the UF2. The loader accepts your local copies of `rom image` and `ram image` from the public `rongarret/gll-mag-patch` archive, reconstructs the fixed-width addressed rows, and verifies:

- ROM: 4096 bytes, CRC32 `37376D80`
- RAM initializer: 1792 bytes, CRC32 `A06242CA`

These values were independently rechecked against the live public archive during GitHub CI. The ROM begins `71 00 C0 04 02 D3 4D B9 ...` and ends `A1 F8 06 AF A7 F8 0F BD F8 8D AD DF 00 00 00 00`.

The earlier development value `779E96F2` for the ROM was incorrect and is intentionally not accepted by this release.

The verified images are stored in the Pico's flash filesystem. After that the Pico is standalone.

## Build

The reference build is pinned to **Earle Philhower Arduino-Pico 6.0.0** and targets **Raspberry Pi Pico** with a 64 KB LittleFS partition. USB `Serial` is USB CDC.

See `LOCAL_BUILD.md` for the exact local build commands.

## Install flight images

```bash
python -m pip install pyserial
python tools/load_galileo_images.py --port COM7 --rom "rom image" --ram "ram image"
```

Linux/macOS example:

```bash
python3 tools/load_galileo_images.py --port /dev/ttyACM0 --rom "rom image" --ram "ram image"
```

## Terminal

After images are installed and verified:

```text
GALILEO MAG / RP2040 CDP1802 emulator
ROM CRC 37376D80 OK
RAM CRC A06242CA OK
GALILEO flight image booted at $0000
OK
```

Verified flight-word examples:

```forth
INB POWER ON
OUT HIGAIN OFF
FLIPPER POWER ON
OUT FLIP LEFT
HEX 7002 C@ U.
GSTATUS
```

Monitor commands:

```text
.RUN
.PAUSE
.RESET
.REGS
.STATUS
.STEP
.BURN 100000
.CMD C6 55
.ERASE
```

`.CMD C6 55` exercises the historical command path: command DMA fixture -> preserved `?COMND` -> preserved `CKCOMM` -> OUTBOARD MAG POWER ON.

### Machine-readable 1802 debug interface

The normal Forth/monitor console remains intact. For deterministic browser, Python, or regression use, commands whose first token begins with an `@` command name access a separate CDP1802 debug protocol:

```text
@HELP
@STATE
@STEP
@RESET
@IRQ
@SETPC 4200
@SET D 55
@EF 1 1
@PEEK 4200 10
@POKE 4200 F8 55 7B 7A 00
```

Replies begin with `@1802`. `@STATE` reports R0-RF plus D, DF, P, X, T, IE, Q, EF1-EF4, PC, IDL/HALT/pending-interrupt state, undefined-opcode trap, instruction count, and machine-cycle count in a stable ASCII record.

The existing Forth `@` fetch word is preserved: a line containing `@` as a Forth token is not treated as a debug command.

See `CDP1802_DEBUG_PROTOCOL.md` for the protocol contract and differential-test sequence. A Python client is provided in `tools/rp2040_1802_debug.py`:

```bash
python3 tools/rp2040_1802_debug.py --self-test
python3 tools/rp2040_1802_debug.py --port COM7
python3 tools/rp2040_1802_debug.py --port COM7 --step
python3 tools/rp2040_1802_debug.py --port COM7 --smoke
```

The hardware smoke test is deliberately reversible: it saves a few bytes in Galileo RAM at `$4200`, installs `LDI 55; SEQ; REQ; IDL`, verifies the expected D/Q/PC/IDL state after each step, restores the original RAM bytes, and finally returns through the normal Galileo reset path.

## Validation

GitHub CI performs all of the following before accepting a build:

1. host CDP1802/MMIO/GY-271 tests;
2. dedicated CDP1802 ISA/state regression tests;
3. debug-protocol parser self-test;
4. fresh download and checksum verification of the preserved archive images;
5. preserved-flight boot/integration regression using those archive images;
6. installation of pinned Arduino-Pico 6.0.0;
7. real Raspberry Pi Pico UF2 cross-compilation;
8. upload of the resulting UF2/ELF/BIN build artifacts.

Host test command:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I. \
    cdp1802.cpp crc32.cpp galileo_machine.cpp gy271_sensor.cpp tests/test_core.cpp \
    -o tests/test_core
./tests/test_core
```

Current core/MMIO/GY-271 result: **27 passed, 0 failed**.

The RP2040 C++ CPU core has also been differential-tested against the independently validated JavaScript CDP1802 implementation across **6,144 generated one-instruction states (24 states × all 256 opcodes): 6,144/6,144 matched**.

The additional `tests/test_1802_regression.cpp` suite is intended as a readable permanent guardrail around the instruction families and state semantics most likely to regress: register access, load/store/indexing, arithmetic/carry/borrow, shifts, short/long branches, Q/EF, INP/OUT, interrupts, undefined-opcode trapping, cycle bookkeeping, and state snapshot/restore.

## Provenance

- RP2040 emulator inspiration: https://github.com/wd5gnr/1802black
- historical Galileo archive: https://github.com/rongarret/gll-mag-patch

The flight ROM/RAM are not modified by this firmware. The Pico supplies the CPU and peripheral environment around them.

## GY-271 physical magnetometer backend

This revision can connect a low-cost GY-271 board to the emulated Galileo ADC without modifying the historical flight ROM.

### Wiring

```text
GY-271       Raspberry Pi Pico
VCC    ->    3V3
GND    ->    GND
SDA    ->    GP4
SCL    ->    GP5
```

The driver uses the RP2040's second core for all I2C traffic. Core 1 continuously samples the physical sensor and publishes the latest field vector into a sequence-locked mailbox. Core 0, which runs the CDP1802 emulator and Galileo MMIO, only copies the latest completed sample; no I2C transaction occurs in an emulated ADC access.

### Silicon detection

GY-271 boards are not trustworthy as part-number labels, so the firmware verifies the device:

- HMC5883L: 7-bit address `0x1E`, ID registers `$0A-$0C` must read `H43`
- QMC5883L: 7-bit address `0x0D`, chip-ID register `$0D` must read `$FF`

The driver then configures:

- HMC5883L: 8-sample averaging, 75 Hz, +/-1.3 gauss, continuous mode
- QMC5883L: OSR=512, 100 Hz, +/-2 gauss, continuous mode

The byte layouts are handled separately: HMC data arrive X/Z/Y big-endian; QMC data arrive X/Y/Z little-endian.

### Galileo mapping

The physical sensor is converted to a signed magnetic field in 0.001 uT units. The first laboratory adapter then maps it to the historical 12-bit ADC using:

```text
0 uT     -> 2048
+1 uT    -> +10 ADC counts
-1 uT    -> -10 ADC counts
```

with saturation to 0..4095.

One GY-271 feeds both instrument triples in this first revision:

```text
ADC 0  INBOARD X    <- GY-271 X
ADC 1  INBOARD Y    <- GY-271 Y
ADC 2  INBOARD Z    <- GY-271 Z
ADC 3  INBOARD REF  <- reference fixture
ADC 4  OUTBOARD X   <- GY-271 X
ADC 5  OUTBOARD Y   <- GY-271 Y
ADC 6  OUTBOARD Z   <- GY-271 Z
ADC 7  OUTBOARD REF <- reference fixture
```

This physical-to-ADC scale is intentionally provisional. It is not asserted to be the original Galileo fluxgate transfer function.

### Terminal commands

```text
.MAGSCAN
.MAGSTATUS
.MAGRAW
.MAGREAL
.MAGSIM
```

After `.MAGREAL`, the preserved Galileo `SAMPLE` routine sees those physical field values through the same `$70yy` ADC MMIO interface it uses in the synthetic model.

### Deliberate limits of this revision

- Galileo `HIGAIN` is not yet mapped to a historically recovered analog gain curve.
- Galileo sensor POWER does not yet electrically gate the modern GY-271.
- The historical flipper is not yet mapped to a physical or mathematical rotation of the GY-271 axes.
- Hard/soft-iron calibration of the GY-271 is not yet applied.
- One physical sensor currently feeds both INBOARD and OUTBOARD channels.