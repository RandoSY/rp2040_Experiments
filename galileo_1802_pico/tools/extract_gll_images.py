#!/usr/bin/env python3
"""Extract verified binary images from Ron Garret's preserved Lisp hex dumps.

This utility contains no historical image bytes. It is useful for private/local
and CI regression tests where the archive files are obtained separately.
"""
from __future__ import annotations
import argparse, pathlib, zlib
from load_galileo_images import parse_archive, ROM_LEN, RAM_LEN, ROM_CRC, RAM_CRC

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--rom',required=True)
    ap.add_argument('--ram',required=True)
    ap.add_argument('--out-dir',required=True)
    args=ap.parse_args()
    out=pathlib.Path(args.out_dir); out.mkdir(parents=True,exist_ok=True)
    rom=parse_archive(args.rom,ROM_LEN,0x0000,16)
    ram=parse_archive(args.ram,RAM_LEN,0x4000,8)
    rc=zlib.crc32(rom)&0xffffffff; mc=zlib.crc32(ram)&0xffffffff
    print(f'ROM {len(rom)} CRC32 {rc:08X} expected {ROM_CRC:08X}')
    print(f'RAM {len(ram)} CRC32 {mc:08X} expected {RAM_CRC:08X}')
    print('ROM first16',rom[:16].hex().upper(),'last16',rom[-16:].hex().upper())
    print('RAM first8',ram[:8].hex().upper(),'last8',ram[-8:].hex().upper())
    if rc!=ROM_CRC: raise SystemExit(f'ROM CRC mismatch {rc:08X} != {ROM_CRC:08X}')
    if mc!=RAM_CRC: raise SystemExit(f'RAM CRC mismatch {mc:08X} != {RAM_CRC:08X}')
    (out/'galileo_rom.bin').write_bytes(rom)
    (out/'galileo_ram_init.bin').write_bytes(ram)
if __name__=='__main__': main()
