# GY-271 bring-up: Galileo physical magnetometer

## Wiring

```text
GY-271   Raspberry Pi Pico
VCC   -> 3V3
GND   -> GND
SDA   -> GP4
SCL   -> GP5
```

## First test

Rebuild and flash the UF2, then open USB CDC serial at 115200 and run:

```text
.MAGSCAN
.MAGSTATUS
```

The firmware accepts either:

```text
HMC5883L at 0x1E with ID H43
```

or:

```text
QMC5883L at 0x0D with chip ID 0xFF
```

Rotate the board and repeat `.MAGRAW`; raw XYZ and field XYZ in uT must change.

When a fresh valid sample exists:

```text
.MAGREAL
```

routes the physical vector to Galileo ADC 0/1/2 and 4/5/6. `.MAGSIM` returns to the deterministic fixture.

Initial adapter:

```text
0 uT = ADC 2048
1 uT = 10 ADC counts
```

This is a laboratory adapter, not a claim of the historical Galileo analog transfer function.
