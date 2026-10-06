/* SPDX-License-Identifier: BSD-2-Clause
 * SPDX-FileCopyrightText: 2026 Stefan Reinauer
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "production.h"

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned char UBYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MUTYPE_NONE 0
#define MUTYPE_68851 '2'
#define MUTYPE_68030 '3'
#define MUTYPE_68040 '4'
#define MUTYPE_68060 '6'
#define MSG_NA "N/A"
#define MSG_UNKNOWN "Unknown"
#define MSG_UNCERTAIN "uncertain"
#define MSG_IN_USE "In Use"
#define get_string(x) (x)
#define debug(...) ((void)0)
struct Library { ULONG lib_Version, lib_Revision; } library = {47, 11}, *MMUBase;
struct { ULONG AttnFlags; } execbase, *SysBase = &execbase;
typedef struct {
    CPUType cpu_type;
    FPUType fpu_type;
    MMUType mmu_type;
    BOOL mmu_present;
    MMUTranslationState mmu_translation;
    char cpu_string[32], mmu_string[32];
} HardwareInfo;
static HardwareInfo hw_info;
static int library_available, library_type, fallback, reads, queried_cpu;
static LONG translation;
static void copy_string(char *dst, const char *src, size_t size)
{ snprintf(dst, size, "%s", src); }
static struct Library *open_mmu_library(void)
{ return library_available ? &library : NULL; }
static int GetMMUType(void) { return library_type; }
static void CloseLibrary(struct Library *base) { assert(base == &library); }
static ULONG GetMMU(ULONG cpu) { assert(cpu); return fallback; }
static LONG GetMMUTranslation(ULONG cpu)
{ reads++; queried_cpu = cpu; return translation; }
#include "functions.h"

static void run(CPUType cpu, int available, int type, LONG state)
{
    memset(&hw_info, 0, sizeof(hw_info));
    hw_info.cpu_type = cpu;
    hw_info.fpu_type = FPU_NONE;
    library_available = available;
    library_type = type;
    fallback = type != MUTYPE_NONE;
    translation = state;
    reads = queried_cpu = 0;
    detect_mmu();
}

int main(void)
{
    static const struct { CPUType cpu; int type, asm_cpu; } cases[] = {
        {CPU_68020, MUTYPE_68851, ASM_CPU_68020},
        {CPU_68030, MUTYPE_68030, ASM_CPU_68030},
        {CPU_68040, MUTYPE_68040, ASM_CPU_68040},
        {CPU_68060, MUTYPE_68060, ASM_CPU_68060}
    };
    char text[128];
    unsigned i;
    int available, state;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (available = 0; available <= 1; ++available) {
            for (state = -1; state <= 1; ++state) {
                run(cases[i].cpu, available, cases[i].type, state);
                assert(hw_info.mmu_present);
                assert(hw_info.cpu_type == cases[i].cpu);
                assert(hw_info.mmu_translation == state);
                assert(reads == 1 && queried_cpu == cases[i].asm_cpu);
                format_mmu_value(text, sizeof(text));
                assert((strstr(text, "In Use") != NULL) == (state == 1));
                assert((strstr(text, "Unknown") != NULL) == (state == -1));
                build_mmu_string(text, sizeof(text), &hw_info);
                assert(strstr(text, state == 1 ? "running" :
                                    state == 0 ? "not active" : "state unknown"));
            }
            run(cases[i].cpu, available, MUTYPE_NONE, 1);
            assert(!hw_info.mmu_present && reads == 0);
            assert(hw_info.mmu_translation == MMU_TRANSLATION_DISABLED);
        }
    }
    /* A type result must not make an absent MMU or special CPU probe TC. */
    run(CPU_68030, 1, 0x7f, 1);
    assert(reads == 0 && !hw_info.mmu_present);
    run(CPU_68080, 1, MUTYPE_68060, 1);
    assert(reads == 0 && hw_info.mmu_translation == MMU_TRANSLATION_UNKNOWN);
    run(CPU_68080, 0, MUTYPE_NONE, 1);
    assert(reads == 0 && hw_info.mmu_translation == MMU_TRANSLATION_UNKNOWN);
    run(CPU_EMU, 1, MUTYPE_68030, 1);
    assert(reads == 0 && hw_info.mmu_translation == MMU_TRANSLATION_UNKNOWN);
    run(CPU_68000, 0, MUTYPE_NONE, 1);
    assert(reads == 0 && !hw_info.mmu_present);
    /* A 68030 may be reported through MMULib's 68851 result. */
    run(CPU_68030, 1, MUTYPE_68851, 1);
    assert(queried_cpu == ASM_CPU_68030);
    puts("PASS: MMU detection, capability preservation and status formatting");
    return 0;
}
