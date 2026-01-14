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
#define ASM_CPU_UNKNOWN  13

ULONG GetCPUReg(void);
ULONG GetCPU060(void);
ULONG GetVBR(void);
ULONG GetMMU( ULONG cpuType __asm("d0"));

#endif /* CPU_H */
