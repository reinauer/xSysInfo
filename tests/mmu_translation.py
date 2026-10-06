#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Stefan Reinauer
"""Exercise the assembled translation probe with injected TC reads/faults.

Requires vasmm68k_mot, vlink, amitools and machine68k. The harness models
Exec Supervisor and TC reads; it does not emulate MMU mappings or prove
physical hardware behavior. Run with: python3 tests/mmu_translation.py
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from amitools.binfmt.BinFmt import BinFmt
from amitools.binfmt.Relocate import Relocate
from machine68k import CPUType, Machine, Register

ROOT = Path(__file__).resolve().parents[1]
CC = Path(shutil.which("m68k-amigaos-gcc")).resolve()
NDK = os.environ.get("NDK_PATH", str(CC.parent.parent / "m68k-amigaos/ndk-include"))
BASE, EXEC, END = 0x10000, 0x4000, 0x8000
USP, SSP = 0xe0000, 0xf0000
VECTORS = (0x10, 0x2c, 0x34)


def check(code, symbols, model, kind, vbr, tc, fault, initial_sr):
    machine = Machine(model, 1024)
    try:
        cpu, mem = machine.cpu, machine.mem
        mem.w_block(BASE, code)
        mem.w32(4, EXEC)
        cpu.w_reg(Register.VBR, vbr)
        for base in {0, vbr}:
            for vector in VECTORS:
                mem.w32(base + vector, 0xdead0000 + vector)
        end = machine.create_execute_end("returned")
        mem.w16(END, 0xa000 | machine.traps.alloc(lambda op, pc: end))
        calls = []

        def supervisor(op, pc):
            # Exec calls A5 with an RTE frame and preserves the caller's SR.
            sp, sr = cpu.r_sp(), cpu.r_sr()
            ret = mem.r32(sp)
            target_sp = sp + 4 if sr & 0x2000 else cpu.r_isp()
            cpu.w_sr((sr | 0x2000) & 0x3fff)
            if not sr & 0x2000:
                cpu.w_usp(sp + 4)
            sp = target_sp - 8
            cpu.w_sp(sp)
            mem.w16(sp, sr)
            mem.w32(sp + 2, ret)
            mem.w16(sp + 6, 0)
            cpu.w_pc(cpu.r_reg(Register.A5))
            calls.append("supervisor")

        mem.w16(EXEC - 30, 0xa000 | machine.traps.alloc(supervisor))

        def read_tc(op, pc):
            assert cpu.r_sr() & 0x700 == 0x700
            calls.append("tc")
            if fault:
                vector, fmt, size = fault
                sp = cpu.r_sp() - size
                mem.w_block(sp, bytes([0x5a]) * size)
                mem.w16(sp, cpu.r_sr())
                mem.w32(sp + 2, pc)
                mem.w16(sp + 6, (fmt << 12) | vector)
                cpu.w_sp(sp)
                cpu.w_pc(mem.r32(vbr + vector))
            else:
                if kind in (3, 5):
                    mem.w32(cpu.r_sp(), tc)
                else:
                    cpu.w_reg(Register.D1, tc)
                cpu.w_pc(pc + 4)

        # Only replace the TC instruction, executing the real guard and
        # enable-bit decoding around it. Assert its direction/encoding.
        for label, opcode in (("mmu_translation_030", b"\xf0\x17\x42\x00"),
                              ("mmu_translation_040", b"\x4e\x7a\x10\x03")):
            pc = symbols[label]
            assert bytes(mem.r_block(pc, 4)) == opcode
            mem.w16(pc, 0xa000 | machine.traps.alloc(read_tc))
        saved = {reg: 0x11220000 + int(reg) for reg in
                 [Register.D2, Register.D3, Register.D4, Register.D5,
                  Register.D6, Register.D7, Register.A2, Register.A3,
                  Register.A4, Register.A5, Register.A6]}
        for reg, value in saved.items():
            cpu.w_reg(reg, value)
        cpu.w_sr(initial_sr)
        cpu.w_isp(SSP)
        cpu.w_usp(USP)
        cpu.w_sp(SSP if initial_sr & 0x2000 else USP)
        start_sp = cpu.r_sp()
        mem.w32(start_sp, END)
        cpu.w_reg(Register.D0, kind)
        cpu.w_pc(symbols["_GetMMUTranslation"])
        result = machine.execute(10000)
        supported = kind in (3, 5, 7, 10)
        expected = (int(bool(tc & (0x80000000 if kind in (3, 5) else 0x8000)))
                    if supported and not fault else 0xffffffff)
        assert result.result is end, (model, kind, fault, result)
        assert cpu.r_reg(Register.D0) == expected, (kind, hex(tc), fault)
        assert cpu.r_sp() == start_sp + 4
        assert cpu.r_sr() & 0x2700 == initial_sr & 0x2700
        assert cpu.r_reg(Register.VBR) == vbr
        for reg, value in saved.items():
            assert cpu.r_reg(reg) == value, reg
        for base in {0, vbr}:
            for vector in VECTORS:
                assert mem.r32(base + vector) == 0xdead0000 + vector
        assert calls == (["supervisor", "tc"] if supported else [])
    finally:
        machine.cleanup()


with tempfile.TemporaryDirectory(prefix="xsysinfo-mmu-") as tmp:
    tmp = Path(tmp)
    stub = tmp / "stub.S"
    stub.write_text(" section Code,code\n xdef _sample_ramsey_refreshes\n"
                    "_sample_ramsey_refreshes: rts\n")
    for source, obj in ((ROOT / "src/cpu.S", tmp / "cpu.o"),
                        (stub, tmp / "stub.o")):
        subprocess.run(["vasmm68k_mot", "-quiet", "-Fhunk", "-I", NDK,
                        "-o", str(obj), str(source)], check=True)
    binary = tmp / "probe"
    subprocess.run(["vlink", "-bamigahunk", "-o", str(binary),
                    str(tmp / "cpu.o"), str(tmp / "stub.o")], check=True)
    image = BinFmt().load_image(str(binary))
    assert len(image.get_segments()) == 1
    segment = image.get_segments()[0]
    symbols = {s.get_name().decode(): BASE + s.get_offset()
               for s in segment.get_symtab().get_symbols()}
    code = bytes(Relocate(image).relocate([BASE])[0])
    count = 0
    for model, kind in ((CPUType.M68020, 3), (CPUType.M68030, 5),
                        (CPUType.M68040, 7), (CPUType.M68040, 10)):
        # The 040 core covers the shared MOVEC path, not native 060 MMU behavior.
        for vbr in (0, 0x6000):
            for sr in (0, 0x0500, 0x2000, 0x2500):
                for tc in (0, 0x7fff7fff, 0x8000, 0x80000000, 0xffffffff):
                    check(code, symbols, model, kind, vbr, tc, None, sr)
                    count += 1
                for vector in VECTORS:
                    for fmt, size in ((0, 8), (2, 12), (11, 92)):
                        check(code, symbols, model, kind, vbr, 0,
                              (vector, fmt, size), sr)
                        count += 1
    for kind in (0, 1, 2, 4, 6, 8, 9, 11, 12, 13, 14, 15):
        check(code, symbols, CPUType.M68020, kind, 0, 0, None, 0)
        count += 1
    print(f"PASS: {count} translation-bit, exception, ABI and vector checks")
