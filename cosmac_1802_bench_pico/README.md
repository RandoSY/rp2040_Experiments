# COSMAC 1802 Bench Pico

A deliberately small stand-alone RCA CDP1802 laboratory target for the Raspberry Pi Pico / RP2040.

This image is **not** a Galileo flight-computer build. It contains no preserved flight ROM/RAM image, no flight scheduler, no magnetometer support, no Forth console, no storage-image loader, and no periodic flight interrupt source.

The machine is intentionally simple:

- 64 KiB RAM
- validated CDP1802 core
- Q output mapped directly to the Pico onboard LED
- 7 input latches and 7 output latches behind the 1802 INP/OUT instructions
- USB serial bench monitor at 115200 baud
- real `RUN`, `STOP`, `STEP`, and `RESET` operations
- adjustable emulated 1802 input clock, default 1.6 MHz

## Bench commands

```text
@RUN
@STOP
@STEP
@RESET
@STATE
@SETPC 0200
@POKE 0200 F8 FF B1 A1 7B ...
@PEEK 0200 20
@SET R0 0200
@SET Q 1
@EF 1 0
@SPEED 1600000
```

For compatibility with the earlier browser dashboard, `.RUN` is accepted as an alias for `@RUN` and `.PAUSE` as an alias for `@STOP`.

`RESET` resets the 1802 CPU but deliberately preserves RAM, matching the needs of a simple load / reset / run bench workflow.

## Q-blink experiment

Load the familiar program at `$0200`, reset, set the PC, and run. `SEQ` drives Q high and lights the Pico LED; `REQ` drives Q low.

```text
@STOP
@POKE 0200 F8 FF B1 A1 7B 21 81 3A 05 91 3A 05 7A F8 FF B1 A1 21 81 3A 11 91 3A 11 F8 FF B1 A1 30 04
@RESET
@SETPC 0200
@RUN
```

## Building

GitHub Actions stages the repository's generic `cdp1802.cpp` and `cdp1802.h` into this sketch directory and compiles with Arduino-Pico 6.0.0 for a generic Raspberry Pi Pico.

For a local Arduino CLI build, copy these two generic core files from `galileo_1802_pico/` into this directory first. They are shared only as a validated processor core; no Galileo machine or application code is linked into the bench image.
