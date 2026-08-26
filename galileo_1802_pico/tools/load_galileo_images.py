#!/usr/bin/env python3
"""Install the preserved Galileo MAG ROM/RAM images into Galileo1802Pico flash.

The script intentionally does not contain or download the historical image bytes.
Point it at locally obtained copies of `rom image` and `ram image` from the
rongarret/gll-mag-patch archive. It parses the original Lisp hex-dump format,
verifies the exact known CRCs, and sends the bytes over the Pico USB CDC port.
"""
from __future__ import annotations
import argparse, re, sys, time, zlib

ROM_LEN=0x1000
RAM_LEN=0x700
ROM_CRC=0x779E96F2
RAM_CRC=0xA06242CA

LINE_RE=re.compile(r"^\s*([0-9A-Fa-f]+)\s+(.+)$")
BYTE_RE=re.compile(r"\b[0-9A-Fa-f]{1,2}\b")

def parse_archive(path: str, expected_len: int, base_addr: int, row_bytes: int) -> bytes:
    """Parse the addressed rows exactly as the validated browser v8 parser does.

    ROM rows contain 16 data bytes and RAM rows contain 8.  Only the first
    row_bytes byte tokens after the address are data; Lisp punctuation or other
    text outside those fixed-width rows is ignored.  Rows that do not contain
    exactly row_bytes bytes are ignored.
    """
    pairs=[]
    with open(path,"r",encoding="utf-8",errors="replace") as f:
        for line in f:
            m=LINE_RE.match(line)
            if not m:
                continue
            addr=int(m.group(1),16)
            vals=[int(x,16) for x in BYTE_RE.findall(m.group(2))[:row_bytes]]
            if len(vals)==row_bytes:
                pairs.append((addr,vals))
    if not pairs:
        raise ValueError(f"{path}: no archive image rows found")
    lo=min(addr for addr,_ in pairs)
    hi=max(addr+row_bytes for addr,_ in pairs)
    if lo != base_addr:
        raise ValueError(f"{path}: image starts at 0x{lo:04X}, expected 0x{base_addr:04X}")
    if hi-base_addr != expected_len:
        raise ValueError(f"{path}: parsed image length {hi-base_addr}, expected {expected_len}")
    out=bytearray(expected_len)
    seen=[False]*expected_len
    for addr,vals in pairs:
        off=addr-base_addr
        if off < 0 or off+row_bytes > expected_len:
            continue
        out[off:off+row_bytes]=bytes(vals)
        for i in range(off,off+row_bytes):
            seen[i]=True
    missing=[i for i,x in enumerate(seen) if not x]
    if missing:
        raise ValueError(f"{path}: missing {len(missing)} byte positions; first is 0x{missing[0]+base_addr:04X}")
    return bytes(out)

def crc(data: bytes) -> int:
    return zlib.crc32(data)&0xffffffff

def read_line(ser, timeout=4.0):
    deadline=time.time()+timeout
    while time.time()<deadline:
        b=ser.readline()
        if b:
            s=b.decode("utf-8","replace").strip()
            if s:
                print("PICO:",s)
                return s
    raise TimeoutError("Pico did not answer")

def wait_for(ser, prefix, timeout=6.0):
    deadline=time.time()+timeout
    while time.time()<deadline:
        s=read_line(ser,max(0.2,deadline-time.time()))
        if s.startswith(prefix): return s
    raise TimeoutError(f"No response starting with {prefix!r}")

def send_image(ser, kind: str, data: bytes, expected_crc: int):
    ser.write(f"!BEGIN {kind} {len(data)} {expected_crc:08X}\n".encode())
    wait_for(ser,"READY ")
    chunk=32
    for off in range(0,len(data),chunk):
        payload=data[off:off+chunk].hex().upper()
        ser.write(f"!DATA {off:04X} {payload}\n".encode())
        wait_for(ser,"OK DATA")
    ser.write(b"!END\n")
    wait_for(ser,"OK STORED")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--port",required=True,help="Pico USB CDC port, e.g. COM7 or /dev/ttyACM0")
    ap.add_argument("--rom",required=True,help="local `rom image` file from gll-mag-patch")
    ap.add_argument("--ram",required=True,help="local `ram image` file from gll-mag-patch")
    ap.add_argument("--baud",type=int,default=115200,help="ignored by USB CDC but required by pyserial")
    args=ap.parse_args()

    rom=parse_archive(args.rom,ROM_LEN,0x0000,16)
    ram=parse_archive(args.ram,RAM_LEN,0x4000,8)
    rc,mc=crc(rom),crc(ram)
    print(f"ROM: {len(rom)} bytes CRC32 {rc:08X}")
    print(f"RAM: {len(ram)} bytes CRC32 {mc:08X}")
    if rc!=ROM_CRC: raise SystemExit(f"ROM CRC mismatch: expected {ROM_CRC:08X}")
    if mc!=RAM_CRC: raise SystemExit(f"RAM CRC mismatch: expected {RAM_CRC:08X}")

    try: import serial
    except ImportError:
        raise SystemExit("pyserial is required: python -m pip install pyserial")
    with serial.Serial(args.port,args.baud,timeout=0.5,write_timeout=3) as ser:
        time.sleep(1.0); ser.reset_input_buffer()
        send_image(ser,"ROM",rom,ROM_CRC)
        send_image(ser,"RAM",ram,RAM_CRC)
        ser.write(b"!BOOT\n")
        wait_for(ser,"OK BOOT",timeout=8)
    print("Galileo images installed and booted.")

if __name__=="__main__": main()
