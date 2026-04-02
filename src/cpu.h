// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Matthias Heinrichs

/*
 * xSysInfo - cpu header
 * this is a bunch of simple asm-functions, which cannot be done in C
 */

#ifndef CPU_H
#define CPU_H

#define ASM_CPU_68000 1
#define ASM_CPU_68010 2
#define ASM_CPU_68020 3
#define ASM_CPU_68EC020 4
#define ASM_CPU_68030 5
#define ASM_CPU_68EC030 6
#define ASM_CPU_68040 7
#define ASM_CPU_68LC040 8
#define ASM_CPU_68EC040 9
#define ASM_CPU_68060 10
#define ASM_CPU_68EC060 11
#define ASM_CPU_68LC060 12
#define ASM_CPU_68080 13
#define ASM_CPU_EMU 14
#define ASM_CPU_UNKNOWN  15

#define ASM_FPU_NONE 0
#define ASM_FPU_68881 1
#define ASM_FPU_68882 2
#define ASM_FPU_68040 3
#define ASM_FPU_68060 4
#define ASM_FPU_68080 5
#define ASM_FPU_UNKNOWN 6


ULONG GetCPUReg(void);
ULONG SetCPUReg( ULONG value __asm("d0"));
ULONG GetCacheBits(void);
ULONG SetCacheBits( ULONG value __asm("d1"),ULONG mask __asm("d2")) ;
ULONG GetCPU060(void);
ULONG GetVBR(void);
ULONG GetMMU( ULONG cpuType __asm("d0"));
UBYTE GetRamseyRev(void);
UBYTE GetRamseyCtrl(void);
double DoFlops( ULONG loops __asm("d0"), ULONG fpuType __asm("d1"));

#endif /* CPU_H */
