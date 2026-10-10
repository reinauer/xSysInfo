// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

/* Query the running PPC kernel; never start or replace one for detection. */
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <utility/tagitem.h>
#include "hardware.h"
#include "debug.h"

/* PowerUP SDK PPCGetAttrs tags and WarpOS SDK GetInfo tags. */
#define PUP_CPU       0x8001f001UL
#define PUP_COUNT     0x8001f002UL
#define PUP_CLOCK     0x8001f003UL
#define PUP_REV       0x8001f004UL
#define PUP_PLL       0x8001f008UL
#define WOS_PVR       0x80102001UL
#define WOS_BUSCLOCK  0x80102006UL
#define WOS_CPUCLOCK  0x80102007UL

/* WarpOS RunPPC ABI. The floating-register storage is not used. */
struct PPCArgs {
    APTR code;
    LONG offset;
    ULONG flags;
    APTR stack;
    ULONG stack_size;
    ULONG regs[15];
    UBYTE fregs[64];
};

static ULONG powerup_attribute(struct Library *base, ULONG tag)
{
    struct TagItem tags[] = {{tag, 0}, {TAG_DONE, 0}};
    register struct Library *a6 __asm("a6") = base;
    register struct TagItem *a0 __asm("a0") = tags;
    register ULONG d0 __asm("d0");
    __asm volatile("jsr -138(a6)" : "=r"(d0), "+r"(a0) : "r"(a6)
                   : "d1", "a1", "cc", "memory");
    return d0;
}

static LONG warpos_call(struct Library *base, struct PPCArgs *args)
{
    register struct Library *a6 __asm("a6") = base;
    register struct PPCArgs *a0 __asm("a0") = args;
    register LONG d0 __asm("d0");
    __asm volatile("jsr -30(a6)" : "=r"(d0), "+r"(a0) : "r"(a6)
                   : "d1", "a1", "cc", "memory");
    return d0;
}

/* Fill the arguments before binding registers: a call such as memset()
 * between a register variable and the asm leaves a0 undefined. */
static LONG warpos_info(struct Library *base, struct TagItem *tags)
{
    struct PPCArgs args;
    memset(&args, 0, sizeof(args));
    args.code = base;
    args.offset = -594; /* GetInfo, called on the PPC by RunPPC */
    args.regs[0] = (ULONG)base; /* r3 */
    args.regs[1] = (ULONG)tags; /* r4 */
    return warpos_call(base, &args);
}

static const char *ppc_name(ULONG version)
{
    switch (version) {
        case 1: return "601";
        case 3: return "603";
        case 4: return "604";
        case 5: return "602";
        case 6: return "603e";
        case 7: return "603ev";
        case 8: return "750";
        case 9: return "604e";
        case 10: return "604ev";
        case 12: return "7400";
        case 0x800c: return "7410";
        case 0x8000: return "7450";
        case 0x8001: return "7455";
        case 0x8002: return "7447";
        case 0x8003: return "7447A";
        case 0x8004: return "7448";
        default: return NULL;
    }
}

void detect_ppc(void)
{
    struct Library *base;
    BOOL warp, powerup;
    ULONG version = 0, revision = 0, clock = 0, bus = 0;
    const char *name;

    /* Classic PPC accelerators have a 68040/060 host and require OS 2+. */
    if (SysBase->LibNode.lib_Version < 37 || !(SysBase->AttnFlags & AFF_68040))
        return;
    Forbid();
    warp = FindName(&SysBase->LibList, (CONST_STRPTR)"powerpc.library") != NULL;
    powerup = FindName(&SysBase->LibList, (CONST_STRPTR)"ppc.library") != NULL;
    Permit();
    if (!warp && !powerup) return;

    /* Prefer WarpOS when a PowerUP compatibility library is also installed. */
    base = OpenLibrary((CONST_STRPTR)(warp ? "powerpc.library" : "ppc.library"), warp ? 13 : 44);
    if (!base) return;
    snprintf(hw_info.ppc_runtime, sizeof(hw_info.ppc_runtime), "%s %u.%u",
             warp ? "WarpOS" : "PowerUP", base->lib_Version, base->lib_Revision);
    if (warp) {
        struct TagItem tags[] = {{WOS_PVR, 0}, {WOS_BUSCLOCK, 0},
                                {WOS_CPUCLOCK, 0}, {TAG_DONE, 0}};
        if (warpos_info(base, tags) == 0 && tags[0].ti_Data) {
            version = tags[0].ti_Data >> 16;
            revision = tags[0].ti_Data & 0xffff;
            bus = tags[1].ti_Data / 10000; /* Hz -> MHz * 100 */
            clock = tags[2].ti_Data / 10000;
        }
    } else if (powerup_attribute(base, PUP_COUNT)) {
        version = powerup_attribute(base, PUP_CPU);
        revision = powerup_attribute(base, PUP_REV) & 0xffff;
        clock = powerup_attribute(base, PUP_CLOCK);
        if (clock < 10000) clock *= 100; else clock = 0;
        /* HID1 PLL ratios for the classic 603e/ev and 604e accelerators.
         * Reserved codes stay unavailable rather than guessing a ratio. */
        if (base->lib_Version >= 45 && (version == 6 || version == 7 || version == 9)) {
            static const UBYTE ratio_x2[16] = {0,0,0,2,4,13,5,9,6,11,8,10,3,12,7,0};
            ULONG pll = powerup_attribute(base, PUP_PLL);
            if (pll < 16 && ratio_x2[pll]) bus = clock * 2 / ratio_x2[pll];
        }
    }
    CloseLibrary(base);
    if (!version || version == 0xffff) return;
    hw_info.ppc_present = TRUE;
    hw_info.ppc_revision = revision;
    hw_info.ppc_mhz = clock;
    hw_info.ppc_bus_mhz = bus;
    name = ppc_name(version);
    if (name) copy_string(hw_info.ppc_string, name, sizeof(hw_info.ppc_string));
    else snprintf(hw_info.ppc_string, sizeof(hw_info.ppc_string), "PVR $%04lX", version);
    debug("    PPC: %s revision $%04lx, clock %lu.%02lu MHz, bus %lu.%02lu MHz, %s\n",
          hw_info.ppc_string, revision, clock / 100, clock % 100,
          bus / 100, bus % 100, hw_info.ppc_runtime);
}
