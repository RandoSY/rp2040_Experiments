# COSMAC 1802 Native microFORTH Bench v8

Stand-alone Raspberry Pi Pico / RP2040 educational computer. No Galileo flight-control software is required or present in this target.

## Native terminal architecture

The browser is a glass terminal. The emulated CDP1802 owns the prompt and Forth language path.

- `EF4` — console receive character available
- `INP 7` — receive one console character
- `OUT 7` — transmit one console character
- `Q` — Pico onboard LED
- `INP 1` — two bench switches
- `OUT 1` — four LEDs / buzzer latch
- `OUT 2` — ADC channel select
- `INP 2`, `INP 3` — low/high parts of a 10-bit ADC value

The native ROM is an independent microFORTH-lineage educational reconstruction; it does not contain FORTH, Inc. commercial ROM bytes. It occupies the protected emulated ROM window `$C000-$DFFF` and cold-boots at `$C000`.

The 1802 itself performs native `KEY`, `EMIT`, line input, tokenization, number conversion, built-in lookup, colon compilation, threaded execution, and `OK` prompting.

## Acceptance session

After flashing the UF2 and connecting the matching browser dashboard, select **PICO 1802** and **BOOT microFORTH**. The terminal should show:

```text
COSMAC 1802 microFORTH
OK
```

Then try:

```forth
2 3 + .
: SQUARE DUP * ;
12 SQUARE .
42 EMIT
QON
QOFF
```

Expected numerical results are `5` and `144`; `42 EMIT` emits `*`; `QON`/`QOFF` operate the physical Pico onboard LED.

## Hidden monitor transport

Normal USB bytes belong to the guest terminal. ASCII GS (`0x1D`) is reserved for browser housekeeping: host monitor requests use `GS ! command LF`, and monitor replies use `GS = response LF`. The dashboard removes these frames from the visible terminal.

## Build

CI is pinned to Arduino-Pico 6.0.0 and also runs the existing CDP1802 ISA regression before compiling the real Pico UF2.
