// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * showattn.c - Display ExecBase->AttnFlags
 * Compile: m68k-amigaos-gcc -O2 -noixemul -o showattn showattn.c
 */

#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>

int main(void)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    UWORD flags = SysBase->AttnFlags;

    printf("AttnFlags: $%04x\n\n", flags);

    printf("CPU:\n");
    printf("  AFB_68010  (0): %s\n", (flags & AFF_68010)  ? "YES" : "no");
    printf("  AFB_68020  (1): %s\n", (flags & AFF_68020)  ? "YES" : "no");
    printf("  AFB_68030  (3): %s\n", (flags & AFF_68030)  ? "YES" : "no");
    printf("  AFB_68040  (7): %s\n", (flags & AFF_68040)  ? "YES" : "no");
    printf("  AFB_68060 (10): %s\n", (flags & (1<<10))    ? "YES" : "no");

    printf("\nFPU:\n");
    printf("  AFB_68881  (4): %s\n", (flags & AFF_68881)  ? "YES" : "no");
    printf("  AFB_68882  (5): %s\n", (flags & AFF_68882)  ? "YES" : "no");
    printf("  AFB_FPU40  (6): %s\n", (flags & AFF_FPU40)  ? "YES" : "no");

    printf("\nOther:\n");
    printf("  AFB_PRIVATE (8): %s\n", (flags & AFF_PRIVATE) ? "YES" : "no");
    printf("  Bit 9:           %s\n", (flags & (1<<9))      ? "YES" : "no");
    printf("  Bit 11:          %s\n", (flags & (1<<11))     ? "YES" : "no");
    printf("  Bit 12:          %s\n", (flags & (1<<12))     ? "YES" : "no");
    printf("  Bit 13:          %s\n", (flags & (1<<13))     ? "YES" : "no");
    printf("  Bit 14:          %s\n", (flags & (1<<14))     ? "YES" : "no");
    printf("  Bit 15:          %s\n", (flags & (1<<15))     ? "YES" : "no");

    return 0;
}
