# CDP1802 hardware debug protocol

This protocol is an additive debug/validation interface for the RP2040 Galileo CDP1802 emulator. It is intended for browser/Web Serial and automated differential testing while leaving the existing Forth console, `.` monitor commands, and `!` image-upload protocol unchanged.

## Design rules

- One ASCII command per line, terminated by LF.
- Debug commands begin with `@`.
- Numeric CPU and memory values are hexadecimal unless stated otherwise.
- Replies begin with `@1802` so a host can separate protocol traffic from human-oriented console output.
- Sending an `@` command from the sketch pauses normal flight execution before the command is executed. This makes snapshots and single-step results deterministic.
- Protocol version: `1`.

## Commands

| Command | Meaning |
| --- | --- |
| `@HELP` | List protocol commands |
| `@STATE` | Return the complete observable CPU state |
| `@STEP` | Execute exactly one CDP1802 instruction/state transition and then return state |
| `@RESET` | Hard-reset only the emulated CDP1802; memory/peripheral state is retained |
| `@IRQ` | Attempt an immediate CDP1802 interrupt and return state |
| `@SETPC 0200` | Set `R(P)`/program counter |
| `@SET R3 1234` | Set one 16-bit register |
| `@SET D 55` | Set D; similarly DF, P, X, T, IE, Q, IDL, HALT |
| `@EF 1 1` | Assert/deassert EF1..EF4 |
| `@PEEK 0200 10` | Read up to `0x40` bytes; count is hexadecimal |
| `@POKE 0200 F8 55 7B 00` | Write up to `0x40` bytes through the machine bus |

## State record

`@STATE` returns a single deterministic record similar to:

```text
@1802 STATE R0=0200 R1=0000 R2=0000 ... RF=0000 D=55 DF=0 P=0 X=0 T=00 IE=1 Q=1 EF=0000 PC=0200 IDL=0 HALT=0 PIRQ=0 UOP=00 INS=0000000000000012 CYC=0000000000000024
```

The fields are intentionally explicit rather than packed binary so the stream remains useful at a terminal as well as from JavaScript/Python.

`INS` and `CYC` are 64-bit hexadecimal counters. `CYC` is in CDP1802 machine cycles; one machine cycle is eight input clocks in the current core model.

## Differential-test sequence

A host can use the RP2040 as one side of a cross-implementation comparison:

```text
@RESET
@POKE 0200 F8 55 7B 7A 00
@SETPC 0200
@STATE
@STEP
@STEP
@STEP
```

The host runs the same bytes and initial state in the JavaScript/reference 1802 model and compares the `@1802 STATE` fields after each step. A mismatch can therefore identify the exact instruction and exact state field that diverged.

## Important scope note

`@PEEK` and `@POKE` access memory through the existing `CDP1802Bus`/`GalileoMachine` mapping. They therefore preserve Galileo MMIO, bank-switching, and write-protection behavior rather than bypassing the machine model. This is deliberate: CPU-only regression can use ordinary RAM addresses, while Galileo integration tests continue to see realistic machine behavior.
