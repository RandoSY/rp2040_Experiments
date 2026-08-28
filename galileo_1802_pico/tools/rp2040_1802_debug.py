#!/usr/bin/env python3
"""Host-side client for the RP2040 CDP1802 @ debug protocol.

The parser has no third-party dependency. Hardware access requires pyserial.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass
from typing import Dict, List, Optional


STATE_PREFIX = "@1802 STATE "
MEM_RE = re.compile(r"^@1802 MEM ADDR=([0-9A-Fa-f]{4}) DATA=([0-9A-Fa-f]*)$")


@dataclass
class CpuState:
    fields: Dict[str, int]

    def __getitem__(self, key: str) -> int:
        return self.fields[key]


def parse_state_line(line: str) -> CpuState:
    line = line.strip()
    if not line.startswith(STATE_PREFIX):
        raise ValueError(f"not an 1802 state record: {line!r}")
    fields: Dict[str, int] = {}
    for token in line[len(STATE_PREFIX):].split():
        if "=" not in token:
            raise ValueError(f"bad state token: {token!r}")
        key, value = token.split("=", 1)
        if key == "EF":
            if len(value) != 4 or any(ch not in "01" for ch in value):
                raise ValueError(f"bad EF field: {value!r}")
            fields[key] = int(value, 2)
        else:
            fields[key] = int(value, 16)

    required = [f"R{x:X}" for x in range(16)] + [
        "D", "DF", "P", "X", "T", "IE", "Q", "EF", "PC",
        "IDL", "HALT", "PIRQ", "UOP", "INS", "CYC",
    ]
    missing = [key for key in required if key not in fields]
    if missing:
        raise ValueError(f"state record missing: {', '.join(missing)}")
    return CpuState(fields)


def parse_mem_line(line: str) -> tuple[int, bytes]:
    m = MEM_RE.match(line.strip())
    if not m:
        raise ValueError(f"not an 1802 memory record: {line!r}")
    data_hex = m.group(2)
    if len(data_hex) % 2:
        raise ValueError("odd-length memory DATA field")
    return int(m.group(1), 16), bytes.fromhex(data_hex)


def self_test() -> None:
    regs = " ".join(f"R{x:X}={x:04X}" for x in range(16))
    line = (
        f"@1802 STATE {regs} D=55 DF=1 P=2 X=3 T=A5 IE=1 Q=0 "
        "EF=1010 PC=0002 IDL=0 HALT=0 PIRQ=0 UOP=00 "
        "INS=0000000000000012 CYC=0000000000000024"
    )
    state = parse_state_line(line)
    assert state["R0"] == 0
    assert state["RF"] == 0x000F
    assert state["D"] == 0x55
    assert state["DF"] == 1
    assert state["EF"] == 0b1010
    assert state["INS"] == 0x12
    assert state["CYC"] == 0x24

    address, data = parse_mem_line("@1802 MEM ADDR=4200 DATA=F8557B7A00")
    assert address == 0x4200
    assert data == bytes([0xF8, 0x55, 0x7B, 0x7A, 0x00])
    print("rp2040_1802_debug parser self-test: PASS")


class Monitor:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0):
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit("pyserial is required for hardware access: python -m pip install pyserial") from exc
        self.ser = serial.Serial(port, baudrate=baud, timeout=timeout)
        time.sleep(0.15)
        self.ser.reset_input_buffer()

    def close(self) -> None:
        self.ser.close()

    def send(self, command: str, wait_for_state: bool = False, wait_for_mem: bool = False) -> List[str]:
        self.ser.write((command.rstrip("\r\n") + "\n").encode("ascii"))
        self.ser.flush()
        lines: List[str] = []
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if not raw:
                if lines and not (wait_for_state or wait_for_mem):
                    break
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            if wait_for_state and line.startswith(STATE_PREFIX):
                break
            if wait_for_mem and line.startswith("@1802 MEM "):
                break
            if line.startswith("@1802 ERR "):
                break
        return lines

    def state(self) -> CpuState:
        lines = self.send("@STATE", wait_for_state=True)
        for line in reversed(lines):
            if line.startswith(STATE_PREFIX):
                return parse_state_line(line)
        raise RuntimeError("@STATE returned no state record")

    def step(self) -> CpuState:
        lines = self.send("@STEP", wait_for_state=True)
        for line in reversed(lines):
            if line.startswith(STATE_PREFIX):
                return parse_state_line(line)
        raise RuntimeError("@STEP returned no state record")

    def peek(self, address: int, count: int) -> bytes:
        lines = self.send(f"@PEEK {address:04X} {count:X}", wait_for_mem=True)
        for line in reversed(lines):
            if line.startswith("@1802 MEM "):
                got_address, data = parse_mem_line(line)
                if got_address != address:
                    raise RuntimeError(f"memory reply address mismatch: {got_address:04X}")
                return data
        raise RuntimeError("@PEEK returned no memory record")


def print_state(state: CpuState) -> None:
    for row in range(4):
        items = []
        for col in range(4):
            n = row * 4 + col
            items.append(f"R{n:X}={state[f'R{n:X}']:04X}")
        print("  ".join(items))
    print(
        f"D={state['D']:02X} DF={state['DF']} P={state['P']:X} X={state['X']:X} "
        f"T={state['T']:02X} IE={state['IE']} Q={state['Q']} EF={state['EF']:04b} "
        f"PC={state['PC']:04X} IDL={state['IDL']} HALT={state['HALT']}"
    )
    print(f"instructions={state['INS']} machine_cycles={state['CYC']}")


def smoke_test(mon: Monitor) -> None:
    """Run a tiny reversible program in Galileo RAM, then restore the bytes."""
    address = 0x4200
    program = bytes([0xF8, 0x55, 0x7B, 0x7A, 0x00])  # LDI 55; SEQ; REQ; IDL
    original = mon.peek(address, len(program))
    wrote_test_program = False
    try:
        lines = mon.send("@POKE 4200 " + " ".join(f"{b:02X}" for b in program))
        if any(line.startswith("@1802 ERR ") for line in lines):
            raise RuntimeError("POKE failed: " + " | ".join(lines))
        wrote_test_program = True
        if mon.peek(address, len(program)) != program:
            raise RuntimeError("RAM readback did not match test program")

        mon.send("@RESET", wait_for_state=True)
        mon.send(f"@SETPC {address:04X}", wait_for_state=True)

        s1 = mon.step()
        assert s1["D"] == 0x55 and s1["PC"] == 0x4202
        s2 = mon.step()
        assert s2["Q"] == 1 and s2["PC"] == 0x4203
        s3 = mon.step()
        assert s3["Q"] == 0 and s3["PC"] == 0x4204
        s4 = mon.step()
        assert s4["IDL"] == 1 and s4["PC"] == 0x4205
        print("RP2040 CDP1802 hardware smoke test: PASS")
    finally:
        if wrote_test_program:
            restore = mon.send("@POKE 4200 " + " ".join(f"{b:02X}" for b in original))
            if any(line.startswith("@1802 ERR ") for line in restore):
                print("WARNING: could not restore RAM smoke-test bytes", file=sys.stderr)
        # Restore the normal Galileo boot path when images are installed.
        mon.send(".RESET")


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true", help="test parsers without hardware")
    ap.add_argument("--port", help="serial port, e.g. COM7 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--step", action="store_true", help="single-step and print state")
    ap.add_argument("--smoke", action="store_true", help="run reversible RAM hardware smoke test")
    ap.add_argument("--command", help="send one raw line and print replies")
    args = ap.parse_args(argv)

    if args.self_test:
        self_test()
        return 0
    if not args.port:
        ap.error("--port is required unless --self-test is used")

    mon = Monitor(args.port, args.baud)
    try:
        if args.command:
            for line in mon.send(args.command, wait_for_state=args.command.upper() in {"@STATE", "@STEP", "@RESET"}):
                print(line)
        elif args.smoke:
            smoke_test(mon)
        elif args.step:
            print_state(mon.step())
        else:
            print_state(mon.state())
    finally:
        mon.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
