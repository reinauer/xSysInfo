// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Hardware detection using identify.library
 */

#include <string.h>
#include <stdio.h>

#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <libraries/identify.h>
#include <resources/cia.h>
#include <mmu/mmubase.h>
#include <mmu/context.h>
#include <mmu/config.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <proto/dos.h>
#include <proto/mmu.h>
#include <proto/expansion.h>
#include <proto/identify.h>

#include "xsysinfo.h"
#include "hardware.h"
#include "wdprobe.h"
#include "locale_str.h"
#include "debug.h"
#include "cpu.h" //for cpu-type/rev
#include "cache.h"
#include "benchmark.h" //for frequencies
#include "berr_trap.h"

typedef struct DeviceTreeProperty {
    struct DeviceTreeProperty *next;
    const char *name;
    ULONG length;
    const void *value;
} DeviceTreeProperty;

typedef struct DeviceTreeNode {
    struct DeviceTreeNode *next;
    struct DeviceTreeNode *parent;
    const char *name;
    struct DeviceTreeNode *children;
    DeviceTreeProperty *properties;
} DeviceTreeNode;

typedef struct DeviceTreeBaseCompat {
    struct Library node;
    struct ExecBase *exec_base;
    DeviceTreeNode *root;
} DeviceTreeBaseCompat;

/* Global hardware info */
HardwareInfo hw_info;

/* External library bases */
struct Library *MMUBase;
extern struct ExecBase *SysBase;
extern struct GfxBase *GfxBase;

/*
 * Main hardware detection function
 */
BOOL detect_hardware(void)
{

    memset(&hw_info, 0, sizeof(HardwareInfo));

    debug("  hw: Detecting Amiga model...\n");
    detect_amiga_model();

    if (!detect_emu68_systems()) {
        debug("  hw: Detecting CPU...\n");
        detect_cpu();
    }
    else {
        debug("  hw: Emu68-System detected...\n");
    }
    debug("  hw: Detecting FPU...\n");
    detect_fpu();
    debug("  hw: Detecting MMU...\n");
    detect_mmu();
    debug("  hw: Loading MMU remap table...\n");
    load_mmu_remap_table();
    debug("  hw: Reading VBR...\n");
    read_vbr();
    debug("  hw: Reading SSP...\n");
    read_ssp();
    debug("  hw: Detecting chipset...\n");
    detect_chipset();
    detect_native_graphics();
    debug("  hw: Detecting system chips...\n");
    detect_system_chips();
    debug("  hw: Detecting clock...\n");
    detect_clock();
    debug("  hw: Detecting batt mem resources...\n");
    detect_batt_mem();
    debug("  hw: Detecting frequencies...\n");
    detect_frequencies();
    debug("  hw: Refreshing cache status...\n");
    refresh_cache_status();
    debug("  hw: Generating comment...\n");
    generate_comment();

    detect_kickstart();

    debug("  hw: Hardware detection complete.\n");
    return TRUE;
}

void detect_kickstart(void)
{
    ULONG physical_rom = 0;

    if (IdentifyBase && IdentifyBase->lib_Version >= 38)
        physical_rom = IdHardwareNumTags(IDHW_ROMVER, TAG_DONE);

    if (physical_rom) {
        hw_info.kickstart_version = physical_rom & 0xffff;
        hw_info.kickstart_revision = physical_rom >> 16;
    } else {
        hw_info.kickstart_version = *((volatile UWORD *)KICK_VERSION);
        hw_info.kickstart_revision = *((volatile UWORD *)KICK_REVISION);
    }

    /* Fallback to exec version if above didn't provide ROM version */
    if (hw_info.kickstart_version == 0) {
        hw_info.kickstart_version = SysBase->LibNode.lib_Version;
        hw_info.kickstart_revision = SysBase->SoftVer;
    }

    /* Get ROM size*/
    UWORD kick_size = *((volatile UWORD *)0xF80000);
    if (kick_size == 0x1111) {
        hw_info.kickstart_size = 256;
    }
    else {
        /* Fallback: default to 512K */
        hw_info.kickstart_size = 512;
    }

    //save SysBase-Version in case we are softkicking
    hw_info.kickstart_patch_version = SysBase->LibNode.lib_Version;
    hw_info.kickstart_patch_revision = SysBase->SoftVer;
}

void detect_amiga_model(void)
{
    STRPTR model = NULL;

    hw_info.amiga_model_id = ~0UL;
    copy_string(hw_info.amiga_model_string, get_string(MSG_NA),
                sizeof(hw_info.amiga_model_string));

    if (!IdentifyBase) {
        return;
    }

    copy_string(hw_info.amiga_model_string, get_string(MSG_UNKNOWN),
                sizeof(hw_info.amiga_model_string));

    hw_info.amiga_model_id = IdHardwareNumTags(IDHW_SYSTEM, TAG_DONE);
    model = IdHardwareTags(IDHW_SYSTEM, TAG_DONE);
    if (model && model[0]) {
        copy_string(hw_info.amiga_model_string, (const char *)model,
                    sizeof(hw_info.amiga_model_string));
    }
}

void detect_native_graphics(void)
{
    hw_info.native_graphics = NATIVE_GRAPHICS_OCS;
    if ((GfxBase->ChipRevBits0 & SETCHIPREV_AA) == SETCHIPREV_AA)
        hw_info.native_graphics = NATIVE_GRAPHICS_AGA;
    else if ((GfxBase->ChipRevBits0 & SETCHIPREV_ECS) == SETCHIPREV_ECS)
        hw_info.native_graphics = NATIVE_GRAPHICS_ECS;
}

static BOOL dt_name_matches(const char *want, const char *name)
{
    if (!want || !name) return FALSE;

    while (*want && *name && *want == *name) {
        want++;
        name++;
    }

    return *want == '\0' && (*name == '\0' || *name == '@');
}

static DeviceTreeNode *dt_find_child(DeviceTreeNode *parent, const char *name)
{
    DeviceTreeNode *node;

    if (!parent) return NULL;

    for (node = parent->children; node; node = node->next) {
        if (dt_name_matches(name, node->name)) {
            return node;
        }
    }

    return NULL;
}

static DeviceTreeProperty *dt_find_property(DeviceTreeNode *node,
                                            const char *name)
{
    DeviceTreeProperty *prop;

    if (!node) return NULL;

    for (prop = node->properties; prop; prop = prop->next) {
        if (prop->name && strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return NULL;
}

static BOOL dt_has_prefix(const char *pos, const char *end,
                          const char *prefix)
{
    while (*prefix) {
        if (pos >= end || *pos != *prefix) {
            return FALSE;
        }
        pos++;
        prefix++;
    }

    return TRUE;
}

static void set_emu68_cpu_revision(DeviceTreeBaseCompat *dt)
{
    DeviceTreeNode *emu68;
    DeviceTreeProperty *idstring;
    const char *str, *end, *ver;
    char revision[sizeof(hw_info.cpu_revision)];
    ULONG i;

    copy_string(hw_info.cpu_revision, get_string(MSG_NA),
                sizeof(hw_info.cpu_revision));
    hw_info.cpu_rev = (UWORD)-1;

    if (!dt || !dt->root) return;

    emu68 = dt_find_child(dt->root, "emu68");
    idstring = dt_find_property(emu68, "idstring");
    if (!idstring || !idstring->value || idstring->length == 0) return;

    str = (const char *)idstring->value;
    end = str + idstring->length;

    for (ver = str; ver < end; ver++) {
        if (dt_has_prefix(ver, end, "Emu68")) {
            ver += 5;
            while (ver < end && *ver == ' ') ver++;
            break;
        }
    }

    if (ver >= end || *ver == '\0') return;

    i = 0;
    while (ver < end && *ver && *ver != ' ' && *ver != '(' &&
           i < sizeof(revision) - 1) {
        revision[i++] = *ver++;
    }
    revision[i] = '\0';

    if (revision[0]) {
        copy_string(hw_info.cpu_revision, revision,
                    sizeof(hw_info.cpu_revision));
    }
}


/*
    Detects emu68 CPUs
    Returns true if emulated CPU is found
*/
BOOL detect_emu68_systems(void)
{
    BOOL retVal = FALSE;
    DeviceTreeBaseCompat *dt;

    debug("  emu68: Scanning for EMU68 devicetree.resource...\n");
    dt = (DeviceTreeBaseCompat *)OpenResource((CONST_STRPTR)"devicetree.resource");
    if (dt != NULL)
    {
        debug("  emu68: devicetree.resource found!\n");
        snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "Emu68");
        hw_info.cpu_type = CPU_EMU;
        set_emu68_cpu_revision(dt);
        retVal = TRUE;
    } else {
        debug("  emu68: NO devicetree.resource found! Assuming real CPU.\n");
    }

    return retVal;
}


/*
 * Detect CPU type and speed
 */
void detect_cpu(void)
{
    /*
     * First check for emulated CPUs
     */
    if (hw_info.cpu_type == CPU_EMU) {
        return;
    }

    /*
     * now determine kickstart:
     *  Kick <=1.3 does only know 68000-68020
     *  Kick <=3.1 does not know >=68060
     */
   UWORD attnFlags = SysBase->AttnFlags;
    if ((attnFlags & (UWORD)AFF_68010) == 0) { // not even a 68010?
        snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68000");
        hw_info.cpu_type = CPU_68000;
    }

    else if ((attnFlags & (UWORD)AFF_68020) == 0) { // not a 68020?
        snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68010");
        hw_info.cpu_type = CPU_68010;
    }

    else { // we now detect 68030 and 68040 manually, because Kick 1.3 does not know about a 68030+
        ULONG oldBits, newBits;

        // now we have at least a 68020/030
        // the CACRF_FreezeI is a 68020/030-only flag!
        oldBits = SetCacheBits(CACRF_FreezeI, CACRF_FreezeI); // can instruction cache be frozen?
        newBits = GetCacheBits();
        SetCacheBits(oldBits & CACRF_FreezeI, CACRF_FreezeI); // reset to old state
        if ((newBits & CACRF_FreezeI) == CACRF_FreezeI)
        {
            oldBits = SetCacheBits(CACRF_IBE, CACRF_IBE); // can instruction burst be enabled?
            newBits = GetCacheBits();
            SetCacheBits(oldBits & CACRF_IBE, CACRF_IBE); // reset to old state
            if ((newBits & CACRF_IBE) == 0)
            {
                // no 68030
                if (*((volatile UWORD *)KICK_VERSION) == *((volatile UWORD *)KICK_VERSION_MIRR))
                { // do we have 24bit mirroring?
                    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC020");
                    hw_info.cpu_type = CPU_68EC020;
                }
                else
                {
                    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68020");
                    hw_info.cpu_type = CPU_68020;
                }
            }
            else
            {
                hw_info.cpu_type = CPU_68030;
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68030");
            }
        }
        else
        {
            // check for Apollo Vampire cores
            //  now it's 68040/060/080 and its derivatives
            ULONG cpuBits = GetCPU060(); // same bits as CPUType
            if (cpuBits == ASM_CPU_68040)
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68040");
                hw_info.cpu_type = CPU_68040;
            }
            else if (cpuBits == ASM_CPU_68060)
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68060");
                hw_info.cpu_type = CPU_68060;
            }
            else if (cpuBits == ASM_CPU_68LC060)
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68LC060");
                hw_info.cpu_type = CPU_68LC060;
            }
            else if (cpuBits == ASM_CPU_68080)
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68080");
                hw_info.cpu_type = CPU_68080;
            }
            else
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "%s",
                         get_string(MSG_UNKNOWN));
                hw_info.cpu_type = CPU_UNKNOWN;
            }
        }
    }

    /* Get CPU revision from identify.library (returns string) */
    if (hw_info.cpu_type == CPU_68060 ||
        hw_info.cpu_type == CPU_68LC060 ||
        hw_info.cpu_type == CPU_68EC060 ||
        hw_info.cpu_type == CPU_68080
    ) {
        hw_info.cpu_rev = detect_cpu_rev();
        hw_info.has_super_scalar = TRUE;
        hw_info.super_scalar_enabled = get_super_scalar_mode();
        snprintf(hw_info.cpu_revision, sizeof(hw_info.cpu_revision), "Rev. %d", hw_info.cpu_rev);
    }
    else {
        hw_info.cpu_rev = -1;
        snprintf(hw_info.cpu_revision, sizeof(hw_info.cpu_revision), "%s",
                 get_string(MSG_NA));
    }
}

UWORD detect_cpu_rev(void)
{
    if (hw_info.cpu_type >= CPU_68060 && hw_info.cpu_type <= CPU_68080) {
        ULONG cpuReg = GetCPUReg();
        cpuReg = cpuReg>>8;
        cpuReg &= 0xFF;
        return (UWORD) cpuReg;
    }
    else {
        return 0L;
    }
}

BOOL get_super_scalar_mode(void)
{
    if (hw_info.cpu_type >= CPU_68060 && hw_info.cpu_type <= CPU_68080) {
        ULONG cpuReg = GetCPUReg();
        cpuReg &= 1L; //lowest bit is super scalar bit
        if (!cpuReg)
            return FALSE;

        if (hw_info.cpu_type <= CPU_68LC060) {
            ULONG cache_bits = GetCacheBits();
            ULONG cache_mask = CACRF_EBC060 | CACRF_ESB060;

            return (cache_bits & cache_mask) == cache_mask;
        }

        return TRUE;
    } else {
        return FALSE;
    }
}

BOOL set_super_scalar_mode(BOOL value)
{
    ULONG cache_bits;

    if (hw_info.cpu_type < CPU_68060 || hw_info.cpu_type > CPU_68080)
        return FALSE;

    SetCPUReg(value ? 1L : 0L);

    /* Also toggle enhanced branch cache and store buffer */
    cache_bits = GetCacheBits();
    if (value)
        cache_bits |= CACRF_EBC060 | CACRF_ESB060;
    else
        cache_bits &= ~(CACRF_EBC060 | CACRF_ESB060);
    SetCacheBits(cache_bits, cache_bits | CACRF_EBC060 | CACRF_ESB060);

    return get_super_scalar_mode();
}

/*
 * Detect FPU type
 */
void detect_fpu(void)
{
    UWORD attnFlags = SysBase->AttnFlags;

    //default values
    hw_info.fpu_type = FPU_UNKNOWN;
    hw_info.fpu_mhz = 0;

    //check for vampire
    if ((attnFlags & (UWORD)AFF_68080) > 0) { //68080 always has a fpu
        snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68080");
        hw_info.fpu_type = FPU_68080;
        hw_info.fpu_enabled = TRUE;
        return;
    }

    //is there any fpu?
    if ((attnFlags & ((UWORD)AFF_68881|(UWORD)AFF_FPU40)) == 0) { //No FPU
        copy_string(hw_info.fpu_string, get_string(MSG_NONE),
                    sizeof(hw_info.fpu_string));
        hw_info.fpu_type = FPU_NONE;
        if (hw_info.cpu_type == CPU_68040) {
            hw_info.cpu_type = CPU_68LC040; //fpu-less cpu
            snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68LC040");
        }
        //the 68060 is distinguished from the 68LC/EC060 by the cpu-id reg from the cpu-detect-function
        if (hw_info.cpu_type == CPU_68060) {
            hw_info.fpu_type = FPU_68060;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68060");
        }
        return;
    }

    //kick 1.3 does not know any better fpu than 68881!
    if ((attnFlags & (UWORD)AFF_68881) > 0) { //68881
        if ((attnFlags & (UWORD)AFF_68882) > 0) { //68882
            hw_info.fpu_type = FPU_68882;
            hw_info.fpu_enabled = TRUE;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68882");
        }
        else {
            hw_info.fpu_type = FPU_68881;
            hw_info.fpu_enabled = TRUE;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68881");
        }
    }

    //is it 040-ish?!
    if ((attnFlags & (UWORD)AFF_FPU40) > 0) {
        if (hw_info.cpu_type == CPU_68040) {
            hw_info.fpu_type = FPU_68040;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68040");
        }
        else if (hw_info.cpu_type == CPU_68060) {
            hw_info.fpu_type = FPU_68060;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68060");
        }
    }

}

/*
 * Detect MMU type
 */
void detect_mmu(void)
{

    ULONG mmuResult = 0;
    ULONG cpuType = 0;
    BOOL fallBack = TRUE;

    // default
    hw_info.mmu_enabled = FALSE;
    hw_info.mmu_type = MMU_NONE;
    copy_string(hw_info.mmu_string, get_string(MSG_NA),
                sizeof(hw_info.mmu_string));

    // first: try mmu.lib
    if ((MMUBase = (struct Library *)OpenLibrary((CONST_STRPTR)"mmu.library", 40L)))
    { // check for mmu.lib
            fallBack = FALSE;
            switch (GetMMUType())
            {
            case MUTYPE_68851:
                if (hw_info.cpu_type == CPU_68030) {
                    hw_info.mmu_type = MMU_68030;
                    copy_string(hw_info.mmu_string, "68030",
                                sizeof(hw_info.mmu_string));
                } else {
                    hw_info.mmu_type = MMU_68851;
                    copy_string(hw_info.mmu_string, "68851",
                                sizeof(hw_info.mmu_string));
                }
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68030:
                hw_info.mmu_type = MMU_68030;
                copy_string(hw_info.mmu_string, "68030",
                            sizeof(hw_info.mmu_string));
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68040:
                hw_info.mmu_type = MMU_68040;
                copy_string(hw_info.mmu_string, "68040",
                            sizeof(hw_info.mmu_string));
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68060:
                hw_info.mmu_type = hw_info.cpu_type == CPU_68080 ?
                    MMU_68080 : MMU_68060;
                copy_string(hw_info.mmu_string,
                            hw_info.cpu_type == CPU_68080 ? "68080" : "68060",
                            sizeof(hw_info.mmu_string));
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_NONE:
                switch (hw_info.cpu_type) // correct cpu-type to ec for relevant cpus
                {
                case CPU_68030:
                    hw_info.cpu_type = CPU_68EC030;
                    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC030");
                    break;
                case CPU_68040:
                case CPU_68LC040:
                    hw_info.cpu_type = CPU_68EC040;
                    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC040");
                    break;
                case CPU_68LC060:
                case CPU_68060:
                    hw_info.cpu_type = CPU_68EC060;
                    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC060");
                    break;
                case CPU_68080:
                    hw_info.mmu_type = MMU_68080;
                    snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68080");
                    break;
                default:
                    hw_info.mmu_type = MMU_NONE;
                    copy_string(hw_info.mmu_string, get_string(MSG_NA),
                                sizeof(hw_info.mmu_string));
                    break;
                }
                break;
            default:
                break;
            }
            CloseLibrary((struct Library *)MMUBase);
        }

    if (fallBack) //mmu.library didn't open
    {
        // GetMMU has to determine if we have a 68020, 030, 040 or 060-MMU (different opcodes)
        switch (hw_info.cpu_type)
        {
        case CPU_68EC020:
        case CPU_68020:
            cpuType = ASM_CPU_68020;
            break;
        case CPU_68EC030:
        case CPU_68030:
            cpuType = ASM_CPU_68030;
            break;
        case CPU_68LC040:
        case CPU_68EC040:
        case CPU_68040:
            cpuType = ASM_CPU_68040;
            break;
        case CPU_68LC060:
        case CPU_68EC060:
        case CPU_68060:
            cpuType = ASM_CPU_68060;
            break;
        case CPU_68080:
            cpuType = ASM_CPU_68080;
            mmuResult = 1; // there is some kind of mmu
            break;
        default:
            cpuType = 0;
            break;
        }


        if (cpuType >= ASM_CPU_68020 && cpuType != ASM_CPU_68080)
        {
            mmuResult = GetMMU(cpuType);
        }

        if (mmuResult > 0)
        {
            // we have an mmu!
            hw_info.mmu_enabled = TRUE;
            switch (hw_info.cpu_type)
            {
            case CPU_68EC020:
            case CPU_68020:
                snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68851 (%s)", get_string(MSG_UNCERTAIN));
                hw_info.mmu_type = MMU_68851;
                break;
            case CPU_68EC030:
            case CPU_68030:
                snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68030 (%s)", get_string(MSG_UNCERTAIN));
                hw_info.mmu_type = MMU_68030;
                break;
            case CPU_68LC040:
            case CPU_68040:
                snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68040 (%s)", get_string(MSG_UNCERTAIN));
                hw_info.mmu_type = MMU_68040;
                break;
            case CPU_68LC060:
            case CPU_68EC060:
            case CPU_68060:
                snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68060 (%s)", get_string(MSG_UNCERTAIN));
                hw_info.mmu_type = MMU_68060;
                break;
            case CPU_68080:
                snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "68080 (%s)", get_string(MSG_UNCERTAIN));
                hw_info.mmu_type = MMU_68080;
                break;
            default:
                copy_string(hw_info.mmu_string, get_string(MSG_UNKNOWN),
                            sizeof(hw_info.mmu_string));
                hw_info.mmu_type = MMU_UNKNOWN;
                break;
            }
        }
        else
        {
            switch (hw_info.cpu_type) // correct cpu-type to ec for relevant cpus
            {
            case CPU_68030:
                hw_info.cpu_type = CPU_68EC030;
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC030");
                break;
            case CPU_68040:
            case CPU_68LC040:
                hw_info.cpu_type = CPU_68EC040;
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC040");
                break;
            case CPU_68LC060:
            case CPU_68060:
                hw_info.cpu_type = CPU_68EC060;
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68EC060");
                break;
            default:
                break;
            }
        }
    }
}

/*
 * MMU remap table: logical address ranges the MMU redirects to other
 * physical memory (MAPP_REMAPPED), e.g. mufastzero moving page zero and
 * ExecBase to fast RAM. Snapshotted once at startup; boot-time
 * remappings do not change while we run. (issue #44)
 */
#define MAX_REMAP_RANGES 16
static struct {
    ULONG lower;
    ULONG higher;
    LONG  delta;
} remap_ranges[MAX_REMAP_RANGES];
static ULONG remap_count = 0;

void load_mmu_remap_table(void)
{
    struct MinList *list;
    struct MappingNode *mn;

    remap_count = 0;

    if (!hw_info.mmu_enabled)
        return;

    MMUBase = (struct Library *)OpenLibrary((CONST_STRPTR)"mmu.library", 40L);
    if (!MMUBase)
        return;

    list = GetMapping(NULL);
    if (list) {
        for (mn = (struct MappingNode *)(list->mlh_Head);
             mn->map_succ && remap_count < MAX_REMAP_RANGES;
             mn = mn->map_succ) {
            if (mn->map_Properties & MAPP_REMAPPED) {
                remap_ranges[remap_count].lower = mn->map_Lower;
                remap_ranges[remap_count].higher = mn->map_Higher;
                remap_ranges[remap_count].delta = (LONG)mn->map_un.map_Delta;
                debug("  hw: MMU remap $%08lx-$%08lx -> $%08lx\n",
                      (ULONG)mn->map_Lower, (ULONG)mn->map_Higher,
                      (ULONG)(mn->map_Lower + mn->map_un.map_Delta));
                remap_count++;
            }
        }
        ReleaseMapping(NULL, list);
    }
    CloseLibrary((struct Library *)MMUBase);
}

/*
 * Translate a logical address to its physical location. Returns the
 * input address unchanged when no MMU remapping applies.
 */
APTR mmu_physical_address(APTR addr)
{
    ULONG address = (ULONG)addr;
    ULONG i;

    for (i = 0; i < remap_count; i++) {
        if (address >= remap_ranges[i].lower &&
            address <= remap_ranges[i].higher) {
            return (APTR)(address + remap_ranges[i].delta);
        }
    }
    return addr;
}

/*
 * Read VBR
 */
void read_vbr(void)
{
    /* Get VBR */
    if (hw_info.cpu_type != CPU_68000 && hw_info.cpu_type != CPU_UNKNOWN) {
        hw_info.vbr = GetVBR();
    }
    else {
        hw_info.vbr = 0;
    }
}

void read_ssp(void)
{
    hw_info.ssp = GetSSP();
}

/*
 * Detect chipset (Agnus/Denise)
 */
void detect_chipset(void)
{
    UWORD tmp, i;
    debug("    chipset: Detecting Paula...\n");

    /*Get Paula revision*/
    hw_info.paula_rev = *((volatile UWORD *)(CUSTOM_PAULA_ID));
    hw_info.paula_rev &= 0x00FE; //mask irrelevant bits
    switch (hw_info.paula_rev) {
        case 0:
            hw_info.paula_type = PAULA_ORIG;
            break;
        case 2:
            hw_info.paula_type = PAULA_SAGA;
            break;
        default:
            hw_info.paula_type = PAULA_UNKNOWN;
            break;
    }

    debug("    chipset: Detecting Denise/Lisa...\n");
    /* Get Denise/Lisa info */
    hw_info.denise_rev = *((volatile UWORD *)(CUSTOM_DENISE_ID));
    hw_info.denise_rev &= 0xFF;
    for (i=0; i<32;++i) {
        tmp = *((volatile UWORD *)(CUSTOM_DENISE_ID)); //OCS Denise puts gibberish on the bus
        tmp &= 0xFF;
        if (tmp != hw_info.denise_rev || hw_info.denise_rev == 0xFF) {
            hw_info.denise_rev = 0;
            break;
        }
    }

    if (hw_info.paula_type == PAULA_SAGA)
        hw_info.denise_type = DENISE_ISABEL;
    else if (hw_info.denise_rev == 0) {
        hw_info.denise_type = DENISE_OCS;
    }
    else if (hw_info.denise_rev == 0xFC) {
        hw_info.denise_type = DENISE_ECS;
    }
    else if (hw_info.denise_rev == 0xF8) {
        hw_info.denise_type = DENISE_LISA;
    }
    else if (hw_info.denise_rev == 0xF0) {
        hw_info.denise_type = DENISE_ISABEL;
    }
    else if (hw_info.denise_rev == 0xF1) {
        hw_info.denise_type = DENISE_MONICA;
    }
    else {
        hw_info.denise_type = DENISE_UNKNOWN;
    }


    debug("    chipset: Detecting Agnus/Alice...\n");
    if (hw_info.paula_type == PAULA_SAGA) {
        hw_info.agnus_type = AGNUS_SAGA;
        hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
    }
    else {
        /* Get Agnus info */
        hw_info.agnus_rev = *((volatile UWORD *)(CUSTOM_AGNUS_ID)); //this is actually the VPOS-capability, from which you can derive the Agnus
        hw_info.agnus_rev &= 0X7F00; //mask Bit 14-8
        hw_info.agnus_rev = (hw_info.agnus_rev>>8); //shift to lower byte


        switch (hw_info.agnus_rev) {
            case 0x00:
            case 0x10: {
                /* OCS timing is fixed. graphics.library determines PAL/NTSC
                 * from the beam timing, covering A1000s whose VPOSR ID
                 * disagrees with their video standard.
                 * ECS upgrades must keep using their own ID below. */
                BOOL is_pal = (GfxBase->DisplayFlags & PAL) != 0;

                //get possible mirrored version (A1000)
                tmp = *((volatile UWORD *)(CUSTOM_AGNUS_ID_MIRR));
                tmp &= 0X7F00; //mask Bit 14-8
                tmp = (tmp>>8); //shift to lower byte
                debug("    chipset: OCS Agnus rev=$%02lx mirror=$%02lx "
                      "DisplayFlags=$%04lx\n",
                      (ULONG)hw_info.agnus_rev, (ULONG)tmp,
                      (ULONG)GfxBase->DisplayFlags);
                if (hw_info.agnus_rev == tmp)
                    hw_info.agnus_type = is_pal ?
                        AGNUS_OCS_PAL : AGNUS_OCS_NTSC;
                else
                    hw_info.agnus_type = is_pal ?
                        AGNUS_OCS_FAT_PAL : AGNUS_OCS_FAT_NTSC;
                hw_info.max_chip_ram = 512 * 1024;  /* 512K */
                break;
            }
            case 0x20: //ECS PAL rev 4
            case 0x21: //ECS PAL rev 5
                hw_info.agnus_type = AGNUS_ECS_PAL;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x30: //ECS NTSC rev 4
            case 0x31: //ECS NTSC rev 5
                hw_info.agnus_type = AGNUS_ECS_NTSC;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x22: //ALICE PAL
            case 0x23: //ALICE PAL
            case 0x24: //ALICE PAL
                hw_info.agnus_type = AGNUS_ALICE_PAL;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x32: //ALICE NTSC
            case 0x33: //ALICE NTSC
            case 0x34: //ALICE NTSC
                hw_info.agnus_type = AGNUS_ALICE_NTSC;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            default:
                hw_info.agnus_type = AGNUS_UNKNOWN;
                hw_info.max_chip_ram = 512 * 1024;  /* 512K */
                break;
        }
    }
}

/*
 * Detect RTC clock chip
 */
void detect_clock(void)
{
    unsigned char val;
    /*
    The clock registers are shifted: (reg*4)+3, so clockregister F becomes 3F
    Clock must be at dc0000 (Only A1000 has the clock somewhere else, A2000BSW?!?)

    Assumption: a trashed clock is untrashed by kickstart-initialization
    According to commodore the working procedure is:
    Read register F: MSM6242B is '0100' and RP5C01A '0000'
    If '0100' is read, MSM6242B should be there
    Because '0000' is not trustworthy do the second check:
    Read register D: MSM6242B is '0000' and RP5C01A '1001' (Timer Enabled,no alarm, Mode 01)
    If not 1001, write 1001! (This causes an MSM6242B to skip 30 secs)
    Now read the 12/24 register (Reg A):
    0001 should appear on RP5C01A
    */
    if (hw_info.gary_type != GARY_A1000) { //this does not work in an stock A1000!
        val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_F));
        val &= RTC_MASK;
        if (val == 0b0100) {
            hw_info.clock_type = CLOCK_MSM6242;
            copy_string(hw_info.clock_string, get_string(MSG_MSM6242B),
                        sizeof(hw_info.clock_string));
            return;
        }
        if (val > 0) { //when val is not 0 we have no RTC!
            hw_info.clock_type = CLOCK_NONE;
            copy_string(hw_info.clock_string, get_string(MSG_CLOCK_NOT_FOUND),
                        sizeof(hw_info.clock_string));
            return;
        }
        val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_D));
        val &= RTC_MASK;
        if (val != 0b1001) { //status not OK?
            //set correct status and try again
            *((volatile unsigned char *)(RTC_BASE+RTC_REG_D)) = 0b1001;
            val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_D));
            val &= RTC_MASK;
        }
        if (val == 0b1001) { //status OK?
            //write something to Reg c. it should be read as 0!
            *((volatile unsigned char *)(RTC_BASE+RTC_REG_C)) = 5;
            val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_C));
            val &= RTC_MASK;
            if (val == 0) { // coming closer: now read register A, which should be '0001!
                val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_A));
                val &= RTC_MASK;
                if (val ==1 ) { // bingo! RP5C01A
                    hw_info.clock_type = CLOCK_RP5C01;
                    copy_string(hw_info.clock_string, get_string(MSG_RP5C01A),
                                sizeof(hw_info.clock_string));
                    return;
                }
            }
        }
    }
    // On a stock A1000 the usual RTC page at $DC0000 is not decoded.
    // A Spirit Insider 1000 maps an MK48T02 TimeKeeper with its clock
    // registers on odd bytes at $DC0FF1..$DC0FFF.  Probe under bus-error
    // protection so we don't crash on machines without the card.
    // Only plain 68000 is supported here (VBR=0 and simple group-0 frame).
    if (hw_info.gary_type == GARY_A1000 && hw_info.cpu_type == CPU_68000) {
        UBYTE sec = 0, min = 0, hour = 0, day = 0, date = 0, mon = 0;
        if (berr_probe_byte(0xDC0FF3, &sec) == 0 &&
            berr_probe_byte(0xDC0FF5, &min) == 0 &&
            berr_probe_byte(0xDC0FF7, &hour) == 0 &&
            berr_probe_byte(0xDC0FF9, &day) == 0 &&
            berr_probe_byte(0xDC0FFB, &date) == 0 &&
            berr_probe_byte(0xDC0FFD, &mon) == 0) {
            /* Validate BCD fields; reject floating bus ($FF) and
             * obvious garbage.  Seconds ST bit (0x80) is masked off. */
            UBYTE s = sec & 0x7F, m = min & 0x7F, h = hour & 0x3F;
            UBYTE dw = day & 0x07, dt = date & 0x3F, mo = mon & 0x1F;
            #define BCD_OK(v,lo,hi) \
                (((v) & 0x0F) <= 9 && ((v) >> 4) <= 9 && \
                 (((v) >> 4) * 10 + ((v) & 0x0F)) >= (lo) && \
                 (((v) >> 4) * 10 + ((v) & 0x0F)) <= (hi))
            if (BCD_OK(s, 0, 59) && BCD_OK(m, 0, 59) &&
                BCD_OK(h, 0, 23) && dw >= 1 && dw <= 7 &&
                BCD_OK(dt, 1, 31) && BCD_OK(mo, 1, 12)) {
                hw_info.clock_type = CLOCK_MK48T02;
                copy_string(hw_info.clock_string, get_string(MSG_MK48T02),
                            sizeof(hw_info.clock_string));
                return;
            }
            #undef BCD_OK
        }
    }

    // if we drop here, we found no clock
    hw_info.clock_type = CLOCK_NONE;
    copy_string(hw_info.clock_string, get_string(MSG_CLOCK_NOT_FOUND),
                sizeof(hw_info.clock_string));
    return;
}

/*
 * Detect if NV-Ram (batt Mem is available and read values)
 * Call after detect_clock and detect_chips!
 */
void detect_batt_mem(void)
{
    hw_info.battMemData.available = openBattMem();
    hw_info.battMemData.valid_data = FALSE;

    if (hw_info.battMemData.available) {
        hw_info.battMemData.valid_data = readBattMem(&hw_info.battMemData);
    }

}

/*
 * Detect Ramsey
 */
void format_ramsey_rev_string(char *buffer, ULONG size)
{
    if (!hw_info.ramsey_rev) {
        copy_string(buffer, get_string(MSG_NA), size);
        return;
    }

    switch (hw_info.ramsey_rev) {
        case 0x0d:
            snprintf(buffer, size, "Ramsey 4 (0D)");
            break;
        case 0x0f:
            snprintf(buffer, size, "Ramsey 7 (0F)");
            break;
        default:
            snprintf(buffer, size, "Unknown (%02X)", hw_info.ramsey_rev);
            break;
    }
}

void detect_ramsey(void)
{

    /* Ramsey (A3000/A4000 RAM controller) */

    if (hw_info.gary_type != FAT_GARY) { // no fat gary -> no ramsey!
        hw_info.ramsey_rev = 0;
        return;
    }

    hw_info.ramsey_rev = GetRamseyRev(); //In A4000: must be done in supervisor mode!
    if ( hw_info.ramsey_rev == 0xFF ||  // unlikely!
        hw_info.ramsey_rev == 0x00 ) // unlikely!
    {
        hw_info.ramsey_rev = 0;
    }else {
        hw_info.ramsey_ctl = GetRamseyCtrl(); //In A4000: must be done in supervisor mode!

        hw_info.ramsey_page_enabled = hw_info.ramsey_ctl & RAMSEY_PAGE_MODE;
        hw_info.ramsey_burst_enabled = hw_info.ramsey_ctl & RAMSEY_BURST_MODE;
        hw_info.ramsey_wrap_enabled = hw_info.ramsey_ctl & RAMSEY_WRAP_MODE;
        /* On Ramsey 04, bit 4 selects RAM width, not skip mode. */
        hw_info.ramsey_skip_enabled = hw_info.ramsey_rev == 0x0f &&
                                     (hw_info.ramsey_ctl & RAMSEY_SKIP_MODE);
        hw_info.ramsey_refresh_rate = (hw_info.ramsey_ctl & RAMSEY_REFRESH_MODE)>>5;
    }
}

const char *get_ramsey_size_string(void)
{
    if (hw_info.ramsey_ctl & RAMSEY_SIZE)
        return get_string(MSG_1M);
    if (hw_info.ramsey_rev == 0x0d &&
        !(hw_info.ramsey_ctl & RAMSEY_SKIP_MODE))
        return get_string(MSG_1MX1);
    return get_string(MSG_256K);
}

/* Read-only 53C770 probe, following ncr7xx's stable register signature. */
static BOOL detect_ncr53c770(unsigned char *revision)
{
    uint8_t prev_gpcntl = 0xff, prev_macntl = 0xff, prev_ctest3 = 0xff;
    BOOL prev_valid = FALSE;
    int attempt;

    for (attempt = 0; attempt < 4; attempt++) {
        uint8_t old_timeout, gpcntl, macntl, ctest3;
        BOOL valid;

        old_timeout = *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG);
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = FAT_GARY_TIME_OUT_DSACK;
        gpcntl = *((volatile uint8_t *)NCR770_GPCNTL_REG);
        macntl = *((volatile uint8_t *)NCR770_MACNTL_REG);
        ctest3 = *((volatile uint8_t *)NCR770_CTEST3_REG);
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = old_timeout;

        /* GPIO0-3 inputs, GPIO4 output; MACNTL chip type 2 is 53C770. */
        valid = ((gpcntl & 0x1f) == 0x0f) && ((macntl >> 4) == 2);
        debug("    ncr770: GPCNTL=%02x MACNTL=%02x CTEST3=%02x candidate=%d\n",
              gpcntl, macntl, ctest3, valid);
        if (valid && prev_valid && gpcntl == prev_gpcntl &&
            macntl == prev_macntl && ctest3 == prev_ctest3) {
            *revision = ctest3 >> 4;
            return TRUE;
        }
        prev_gpcntl = gpcntl;
        prev_macntl = macntl;
        prev_ctest3 = ctest3;
        prev_valid = valid;
        if (attempt < 3)
            Delay(1);
    }
    return FALSE;
}

/* Detect onboard NCR SCSI or the A3000 SDMAC revision. */
void detect_sdmac(void)
{
    unsigned char ncr_rev;
    uint32_t ovalue, rvalue;
    uint8_t sdmac_version = 0;
    uint8_t istr;
    int pass;
    uint8_t old_timeout;
    hw_info.sdmac_rev = 0;
    hw_info.sdmac_present = FALSE;
    hw_info.resdmac_version = 0;
    hw_info.ncr_rev = 0;
    hw_info.ncr_type = NCR_NONE;

    if (hw_info.gary_type == FAT_GARY)
    { // you need fat gary to access ncr!
        /* The 53C770 has a different register map and revision register. */
        if (detect_ncr53c770(&hw_info.ncr_rev)) {
            hw_info.ncr_type = NCR_53C770;
            return;
        }

        // Switch to DSACK timeout to avoid bus errors when probing
        old_timeout = *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG);
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = FAT_GARY_TIME_OUT_DSACK;

        // now test for A4000T NCR53C710: upper four bits of CTEST8-register contains the chip-rev.
        ncr_rev = *((volatile unsigned char *)(NCR_CTEST8_REG));
        ncr_rev = (ncr_rev & 0xF0) >> 4; // only upper four bits matter
        if (ncr_rev != 0 && ncr_rev != 0xF)
        {
            hw_info.ncr_rev = ncr_rev;
            hw_info.ncr_type = NCR_53C710;
        }

        // Restore original timeout mode
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = old_timeout;
    }

    /* SDMAC access requires Fat Gary and Ramsey. */
    if (hw_info.ramsey_rev > 0 && hw_info.gary_type == FAT_GARY &&
        hw_info.ncr_type == NCR_NONE) {
        // Switch to DSACK timeout to avoid bus errors on A4000
        old_timeout = *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG);
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = FAT_GARY_TIME_OUT_DSACK;

        /* ReSDMAC exposes four ASCII bytes (e.g. "v1.2"), not a chip
         * revision byte. Validate the whole signature and its stability. */
        rvalue = *(volatile uint32_t *)SDMAC_REVISION;
        if ((rvalue & 0xff00ff00UL) == 0x76002e00UL &&
            ((rvalue >> 16) & 0xff) >= '0' &&
            ((rvalue >> 16) & 0xff) <= '9' &&
            (rvalue & 0xff) >= '0' && (rvalue & 0xff) <= '9') {
            (void)*(volatile uint8_t *)RAMSEY_VER;
            if (rvalue == *(volatile uint32_t *)SDMAC_REVISION) {
                hw_info.resdmac_version = rvalue;
                hw_info.sdmac_present = TRUE;
                hw_info.sdmac_rev = 4;
            }
        }
        if (hw_info.resdmac_version)
            goto sdmac_done;
        if (hw_info.sdmac_rev == 0) {
            /* Quick check: ISTR bits - FIFO cannot be both empty and full */
            istr = *SDMAC_ISTR;
            if (istr == 0xff)
                goto sdmac_done;
            if ((istr & SDMAC_ISTR_FIFOE) && (istr & SDMAC_ISTR_FIFOF))
                goto sdmac_done;
            /* ASR is read-only and does not acknowledge WD interrupts.
             * Reserved bits 2/3 distinguish open bus from a WD33C93. */
            if (*SDMAC_WD_ASR & WD_ASR_RESERVED)
                goto sdmac_done;
            hw_info.sdmac_present = TRUE;
            sdmac_version = 2; //default version
            /* Probe WTC registers to distinguish SDMAC-02 from SDMAC-04 */
            for (pass = 0; pass < 6; pass++) {
                uint32_t wvalue;
                switch (pass) {
                    case 0: wvalue = 0x00000000; break;
                    case 1: wvalue = 0xffffffff; break;
                    case 2: wvalue = 0xa5a5a5a5; break;
                    case 3: wvalue = 0x5a5a5a5a; break;
                    case 4: wvalue = 0xc2c2c3c3; break;
                    case 5: wvalue = 0x3c3c3c3c; break;
                }
                Disable();
                /* Never change a transfer count while SCSI is active or
                 * awaiting service (INT, BSY, CIP, DBR in the WD ASR). */
                if ((*SDMAC_WD_ASR & (WD_ASR_ACTIVE | WD_ASR_RESERVED)) ||
                    *SDMAC_ISTR != SDMAC_ISTR_FIFOE) {
                    Enable();
                    goto sdmac_done;
                }
                ovalue = *(volatile uint32_t *)SDMAC_WTC;
                *(volatile uint32_t *)SDMAC_WTC = wvalue;
                (void) *(volatile uint32_t *)RAMSEY_VER; /* Push write to bus */
                rvalue = *(volatile uint32_t *)SDMAC_WTC;
                *(volatile uint32_t *)SDMAC_WTC = ovalue;
                (void) *(volatile uint32_t *)RAMSEY_VER;
                Enable();
                if (rvalue == wvalue) {
                    if ((wvalue != 0x00000000) && (wvalue != 0xffffffff)) {
                        sdmac_version = 0; /* Detection failed */
                        hw_info.sdmac_present = FALSE;
                        goto sdmac_done;
                    }
                } else if (((rvalue ^ wvalue) & 0x00ffffff) == 0) {
                    /* SDMAC-02: only upper byte differs */
                } else if ((rvalue & (1U << (2))) == 0) {
                    /* SDMAC-04: bit 2 is always 0 */
                    if (wvalue & (1U << (2)))
                        sdmac_version = 4;
                }
                else {
                    sdmac_version = 0; /* Detection failed */
                    hw_info.sdmac_present = FALSE;
                    goto sdmac_done;
                }
            }
            hw_info.sdmac_rev = sdmac_version;
        }
sdmac_done:
        // Restore original timeout mode
        *((volatile uint8_t *)FAT_GARY_TIME_OUT_REG) = old_timeout;
    }
}

void format_dma_string(char *buffer, ULONG size)
{
    const char *name = get_string(hw_info.resdmac_version ?
                                  MSG_RESDMAC : MSG_SDMAC);

    if (!hw_info.sdmac_present)
        copy_string(buffer, get_string(MSG_NONE), size);
    else if (hw_info.resdmac_version || !hw_info.sdmac_rev)
        copy_string(buffer, name, size);
    else
        snprintf(buffer, size, "%s rev %02X", name, hw_info.sdmac_rev);
}

void format_resdmac_version(char *buffer, ULONG size)
{
    ULONG version = hw_info.resdmac_version;

    snprintf(buffer, size, "%c%c%c%c", (int)(version >> 24),
             (int)((version >> 16) & 0xff), (int)((version >> 8) & 0xff),
             (int)(version & 0xff));
}

void format_scsi_chip_string(char *buffer, ULONG size)
{
    LocaleStringID name;

    if (hw_info.ncr_type == NCR_53C770)
        name = MSG_NCR_53C770;
    else if (hw_info.ncr_type == NCR_53C710)
        name = MSG_NCR_53C710;
    else {
        if (hw_info.sdmac_present && wd_info.chip != WD_UNKNOWN) {
            name = wd_info.chip == WD_33C93B ? MSG_WD33C93B :
                   wd_info.chip == WD_33C93A ? MSG_WD33C93A : MSG_WD33C93;
            copy_string(buffer, get_string(name), size);
            return;
        }
        /* The A3000 pairs Super DMAC with a WD33C93-compatible chip.
         * Exact silicon/firmware identification requires a controller
         * reset. Do not infer the suffix or read CDB1 as a revision:
         * once the driver is running CDB1 contains command data. */
        copy_string(buffer, get_string(hw_info.sdmac_present ?
                                       MSG_WD33C93_FAMILY : MSG_NONE), size);
        return;
    }

    snprintf(buffer, size, "%s rev %02X", get_string(name), hw_info.ncr_rev);
}


/*
 * Detect Gary
 */
void detect_gary(void)
{

    UWORD testVal1, testVal2, i,j ;
    unsigned char val, val2, tmp,tmp2;

    /*tricky part for manual detection:
        FatGary has a PowerUp-Detect-register at DE0002. However, it is read/writeable.
        If the value changes with the writes, it's a FatGary

        A1000 mirrors the chipregisters.
        A safe register to read is DMACONR. If this register is mirrored A1000 is there.

        A600/A1200 have an undocumented revision register at DE1000, but the 8-bit code is
        "morsed" only via the highest byte

        If this is "FF" or unstable, a A500/2000 is present.
    */
    hw_info.gary_type = GARY_UNKNOWN;

    /*
    A3000(T/+), A4000(T)
    We have to do this first, because a A3000 with a 67040 crashes on the A1000-tests due to mmu-restrictions
    Test, if we have the Power-Up-Register (A3000/4000).
    This writes a 0x80 and a zero to DMACONR on a A1000, which should be safe
    */
    val =  *((volatile unsigned char *)(FAT_GARY_POWER_REG)); //save old value
    //write a value
    *((volatile unsigned char *)FAT_GARY_POWER_REG) = FAT_GARY_POWER_CYCLE; //set bit 7
    //read something from the bus to clear sticky bus
    testVal1 = *((volatile UWORD *)(CUSTOM_JOY0DAT));
    //read back POWER register
    tmp = *((volatile unsigned char *)(FAT_GARY_POWER_REG));
    tmp &= FAT_GARY_POWER_CYCLE; //mask bus rubbish
    if (tmp == FAT_GARY_POWER_CYCLE) {
        //write a zero to MSB
        *((volatile unsigned char *)FAT_GARY_POWER_REG) = FAT_GARY_POWER_GOOD;
        //read something from the bus to clear sticky bus
        testVal1 = *((volatile UWORD *)(CUSTOM_JOY0DAT));
        //read back
        tmp = *((volatile unsigned char *)(FAT_GARY_POWER_REG));
        tmp &= FAT_GARY_POWER_CYCLE; //mask bus rubbish
        if (tmp == FAT_GARY_POWER_GOOD) {
            hw_info.gary_type = FAT_GARY;
            //correct agnus type if necessary
            if (hw_info.agnus_type == AGNUS_ECS_NTSC) { //A3000(T)
                hw_info.agnus_type = AGNUS_ECS_B_NTSC;
            }
            if (hw_info.agnus_type == AGNUS_ECS_PAL) { //A3000(T)
                hw_info.agnus_type = AGNUS_ECS_B_PAL;
            }
            //restore old value
            *((volatile unsigned char *)FAT_GARY_POWER_REG) = val;
            return;
        }
    }


    //test for mirroring (A1000/ A2000BSW)
    testVal1 = *((volatile UWORD *)(CUSTOM_JOY0DAT));
    testVal2 = *((volatile UWORD *)(CUSTOM_JOY1DAT)); //avoid bus stickiness (A3000)
    testVal2 = *((volatile UWORD *)(CUSTOM_JOY0DAT_MIRR));
    if (testVal1 == testVal2) {
        //do another test to be safe
        testVal1 = *((volatile UWORD *)(CUSTOM_JOY1DAT));
        testVal2 = *((volatile UWORD *)(CUSTOM_JOY0DAT)); //avoid bus stickiness (A3000)
        testVal2 = *((volatile UWORD *)(CUSTOM_JOY1DAT_MIRR));
        if (testVal1 == testVal2) {
            hw_info.gary_type = GARY_A1000;
            /* An OCS A1000 has a DIP Agnus; the VPOSR mirror test in
             * detect_chipset() can misread Fat Agnus on expanded machines.
             * This also matches the German A2000-A.
             * Leave ECS upgrades such as the Rejuvenator unchanged. */
            if (hw_info.agnus_type == AGNUS_OCS_FAT_NTSC) {
                hw_info.agnus_type = AGNUS_OCS_NTSC;
                debug("    systemchips: A1000 Gary, correcting Agnus to DIP NTSC\n");
            }
            if (hw_info.agnus_type == AGNUS_OCS_FAT_PAL) {
                hw_info.agnus_type = AGNUS_OCS_PAL;
                debug("    systemchips: A1000 Gary, correcting Agnus to DIP PAL\n");
            }
            return;
        }
    }


    /*
    Leftovers:
    A500, A2000, CDTV, A600/A1200:
    now we read the GAYLE_ID: Write a zero to the ID-register and read it back 8 times!
    */

    for (j=0;j<4;++j) {
        val = 0;
        //test for mirroring (A500)
        tmp2 = *((volatile unsigned char *)(CUSTOM_BLTDDAT));
        *((volatile unsigned char *)(GAYLE_ID)) = 0;
        for (i=0; i<8; i++) {
            tmp = *((volatile unsigned char *)(GAYLE_ID));
            if (i == 0 && tmp == tmp2) { //a500 gary!
                hw_info.gary_type = GARY_A500;
                return;
            }
            //mask
            tmp &= 0x80>>i;
            val = val|(tmp>>i);
        }
        if (j==0) {
            val2 = val;
        }
        else {
            if (val2 != val) { //inconsistent results -> A500 gary
                hw_info.gary_type = GARY_A500;
                return;
            }
        }
    }
    if (val!=0xFF && val !=0) {
        hw_info.gary_type = GAYLE;
        hw_info.gary_rev = val;
        return;
    }

    hw_info.gary_type = GARY_A500;
    return;
}


/*
 * Detect Ramsey, Gary, and expansion slots
 */
void detect_system_chips(void)
{


    debug("    systemchips: Detecting Gary...\n");
    detect_gary();
    debug("    systemchips: Detecting Ramsey...\n");
    detect_ramsey();
    debug("    systemchips: Detecting SDMAC...\n");
    detect_sdmac();


    debug("    systemchips: Detecting bus system...\n");
    /* Check for expansion slots */
    /* Check for Zorro slots */
    hw_info.has_zorro_slots = FALSE;
    hw_info.has_pcmcia = FALSE;
    snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
             "%s", get_string(MSG_NA));

    /* Prefer real hardware: if card.resource exists, PCMCIA is present */
    debug("    systemchips: Checking card.resource...\n");
    if (OpenResource((CONST_STRPTR)"card.resource") != NULL) {
        hw_info.has_pcmcia = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_SLOT_PCMCIA));
        debug("    systemchips: card.resource found, using PCMCIA\n");
        return;
    }
    debug("    systemchips: card.resource not found\n");

    /* Detect Zorro capability based on chipset presence:
     * - Ramsey (memory controller) indicates A3000/A4000 = Zorro III
     * - Gary without Ramsey indicates A500/A2000 = Zorro II
     * This is more reliable than system type detection which can fail
     * with CPU upgrades (e.g., 68060)
     */
    if (hw_info.gary_type == FAT_GARY) {
        /* A3000/A4000 have Fatz Gary - these have Zorro III slots */
        hw_info.has_zorro_slots = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_ZORRO_III));
        debug("    systemchips: Fat Gary detected, using Zorro III\n");
        return;
    }

    if (hw_info.gary_type == GAYLE) {
        /* A600 A1200 have a Gayle chip with ID*/
        hw_info.has_pcmcia = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                "%s", get_string(MSG_SLOT_PCMCIA));
        debug("    systemchips: Gayle detected, using PCMCIA\n");
        return;
    }

    /* A500/A1000/A2000/CDTV*/
    hw_info.has_zorro_slots = TRUE;
    if (hw_info.gary_type == GARY_A1000) {
        /* The A1000 86-pin edge connector predates Zorro II; show plain
         * ZORRO. The German A2000-A is mislabelled by this
         * (same chipset, real Zorro II slots), accepted as a corner case. */
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_SLOT_ZORRO));
        debug("    systemchips: A1000 Gary detected, using Zorro\n");
    } else {
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_ZORRO_II));
        debug("    systemchips: Gary type %ld, using Zorro II\n",
              (long)hw_info.gary_type);
    }
    return;
}

/*
 * Detect display and system timing frequencies
 */
void detect_frequencies(void)
{
    /* PAL/NTSC detection */
    hw_info.is_pal = (GfxBase->DisplayFlags & PAL) ? TRUE : FALSE;

    if (hw_info.is_pal) {
        hw_info.horiz_freq = 15625;     /* 15.625 kHz */
        copy_string(hw_info.mode_string, get_string(MSG_MODE_PAL),
                    sizeof(hw_info.mode_string));
    } else {
        hw_info.horiz_freq = 15734;     /* 15.734 kHz */
        copy_string(hw_info.mode_string, get_string(MSG_MODE_NTSC),
                    sizeof(hw_info.mode_string));
    }

    /* Exec reports these independently: a PAL display can coexist with
     * a 60 Hz supply tick (issue #57). Both fields exist on Kickstart 1.3. */
    hw_info.vert_freq = SysBase->VBlankFrequency;
    hw_info.supply_freq = SysBase->PowerSupplyFrequency;

    debug("    frequencies: DisplayFlags=$%04lx VBlankFrequency=%lu "
          "PowerSupplyFrequency=%lu\n",
          (ULONG)GfxBase->DisplayFlags, hw_info.vert_freq, hw_info.supply_freq);

    /* EClock frequency from exec */
    hw_info.eclock_freq = SysBase->ex_EClockFrequency;
}

/*
 * Refresh cache status from current CACR
 */
void refresh_cache_status(void)
{
    ULONG cacr_bits = 0;

    /*
     * Determine available cache features based on CPU type.
     *
     * For 68040/060/080-class CPUs we expose a real D-cache control and
     * keep the old "CBack" pseudo-feature hidden, because our current
     * cache-bit mapping collapses those controls onto the same hardware
     * state on these CPUs.
     */
    hw_info.has_icache = (hw_info.cpu_type >= CPU_68020);
    hw_info.has_dcache = (hw_info.cpu_type == CPU_68030 ||
                          hw_info.cpu_type == CPU_68EC030 ||
                          (hw_info.cpu_type >= CPU_68040 &&
                           hw_info.cpu_type <= CPU_68080));
    hw_info.has_iburst = (hw_info.cpu_type == CPU_68030 ||
                          hw_info.cpu_type == CPU_68EC030);
    hw_info.has_dburst = (hw_info.cpu_type == CPU_68030 ||
                          hw_info.cpu_type == CPU_68EC030);
    hw_info.has_copyback = FALSE;
    hw_info.has_super_scalar = (hw_info.cpu_type >= CPU_68060 &&
                                hw_info.cpu_type <= CPU_68080);

    /*
     * Prefer Exec's abstracted cache state when available.
     *
     * On 68040/060 systems, CacheControl() reports the generic Amiga
     * CACRF_* view, while Kickstart 1.3 requires raw CACR access.
     */
    if (hw_info.cpu_type >= CPU_68020) {
        if (SysBase->LibNode.lib_Version >= 36 &&
            hw_info.cpu_type != CPU_68080) {
            cacr_bits = CacheControl(0, 0);
        } else {
            cacr_bits = convert68040to68030(GetCacheBits());
        }
    }

    hw_info.icache_enabled = (cacr_bits & CACRF_EnableI) &&
                             hw_info.has_icache ? TRUE : FALSE;
    hw_info.dcache_enabled = (cacr_bits & CACRF_EnableD) &&
                             hw_info.has_dcache ? TRUE : FALSE;
    hw_info.iburst_enabled = (cacr_bits & CACRF_IBE) &&
                             hw_info.has_iburst ? TRUE : FALSE;
    hw_info.dburst_enabled = (cacr_bits & CACRF_DBE) &&
                             hw_info.has_dburst ? TRUE : FALSE;
    hw_info.copyback_enabled = (cacr_bits & CACRF_CopyBack) &&
                               hw_info.has_copyback ? TRUE : FALSE;
    hw_info.super_scalar_enabled = get_super_scalar_mode() ? TRUE : FALSE;
}

/*
 * Get CPU frequency from identify.library (scaled by 100)
 * Falls back to estimates based on CPU type if not available
 */
ULONG measure_cpu_frequency(void)
{
    ULONG speed_mhz = 0;

    if (speed_mhz > 0 && speed_mhz < 1000) {
        return speed_mhz * 100;
    }

    /* Fallback: estimate based on CPU type and system */
    switch (hw_info.cpu_type) {
        case CPU_68000:
        case CPU_68010:
            return 709;   /* Standard 68000 */
        case CPU_68020:
        case CPU_68EC020:
            return 1400;  /* Common for A1200/accelerators */
        case CPU_68030:
        case CPU_68EC030:
            return 2500;  /* Common for 030 accelerators */
        case CPU_68040:
        case CPU_68LC040:
        case CPU_68EC040:
            return 2500;  /* A4000 stock */
        case CPU_68060:
        case CPU_68EC060:
        case CPU_68LC060:
            return 5000;  /* Common 060 speed */
        default:
            return 709;
    }
}
