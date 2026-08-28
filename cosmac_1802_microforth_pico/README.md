# COSMAC 1802 microFORTH Native Bench

Standalone Raspberry Pi Pico/RP2040 educational CDP1802 bench firmware. It intentionally contains no Galileo flight-control software.

The emulated memory includes an independently assembled microFORTH-lineage native kernel at `$C000-$C2C6`. It is **not** a byte-for-byte redistribution of FORTH, Inc.'s commercial 1978 ROM. The USB text parser remains firmware-side for immediate usability, while the supported arithmetic/stack/memory/Q/I/O primitives and compiled colon definitions execute actual CDP1802 machine instructions.

On boot the terminal enters microFORTH mode and prints `OK`. Try:

```forth
2 3 + .
: SQUARE DUP * ;
12 SQUARE .
QON
QOFF
15 OUT1
```

`QON`/`QOFF` drive the emulated 1802 Q output and therefore the Pico onboard LED. `OUT1` drives the simple bench output latch; `IN1`, `IN2`, and `IN3` read switches/ADC values supplied by the browser dashboard.

Type `MACHINE` or send `@MACHINE` to return to the minimal machine-language bench. The basic monitor remains available: `@RUN`, `@STOP`, `@STEP`, `@RESET`, `@STATE`, `@SETPC`, `@POKE`, `@PEEK`, `@SET`, `@SPEED`, `@IO`, and `@IOSET`.

The generic CDP1802 core is staged from `galileo_1802_pico/cdp1802.{cpp,h}` during CI so this target uses the same validated processor implementation without carrying any Galileo application firmware into the bench image.
