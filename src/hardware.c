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

#include "xsysinfo.h"
#include "hardware.h"
#include "locale_str.h"
#include "debug.h"
#include "cpu.h" //for cpu-type/rev
#include "benchmark.h" //for frequencies

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
    debug("  hw: Reading VBR...\n");
    read_vbr();
    debug("  hw: Detecting chipset...\n");
    detect_chipset();
    debug("  hw: Detecting system chips...\n");
    detect_system_chips();
    debug("  hw: Detecting clock...\n");
    detect_clock();
    debug("  hw: Detecting batt mem ressources...\n");
    detect_batt_mem();
    debug("  hw: Detecting frequencies...\n");
    detect_frequencies();
    debug("  hw: Refreshing cache status...\n");
    refresh_cache_status();
    debug("  hw: Generating comment...\n");
    generate_comment();

    /* Get Kickstart info */
    UWORD kick_version = *((volatile UWORD *)KICK_VERSION);
    UWORD kick_revision = *((volatile UWORD *)KICK_REVISION);
    hw_info.kickstart_version = kick_version;
    hw_info.kickstart_revision = kick_revision;

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

    debug("  hw: Hardware detection complete.\n");
    return TRUE;
}


/*
    Detects emu68 CPUs
    Returns true if emulated CPU is found
*/
BOOL detect_emu68_systems(void)
{
    struct ConfigDev *cd = NULL;
    struct Library *ExpansionBase;
    BOOL retVal = FALSE;
    debug("  emu68: Opening expansion.library...\n");
    ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", MIN_EXPANSION_VERSION);
    if (!ExpansionBase) {
        Printf((CONST_STRPTR)"Could not open expansion.library v%d\n", MIN_EXPANSION_VERSION);
    }
    else {

        debug("  emu68: Scanning for EMU68-ConfigDev...\n");
        if ((cd = FindConfigDev(cd, 28019, 1)) != NULL) {
            snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "Emu68");
            hw_info.cpu_type = CPU_EMU;
            retVal = TRUE;
        }
        debug("  emu68: Closing expansion.library...\n");
        CloseLibrary(ExpansionBase);
    }

     return retVal;
}


/*
 * Detect CPU type and speed
 */
void detect_cpu(void)
{
    /*
    first determine kickstart:
    Kick <=1.3 does only know 68000-68020
    Kick <=3.1 does not know >=68060
    */
   UWORD attnFlags = SysBase->AttnFlags;
    if ((attnFlags & (UWORD)AFF_68010) == 0) { //not even a 68010?
        snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68000");
        hw_info.cpu_type = CPU_68000;
    }

    else if ((attnFlags & (UWORD)AFF_68020) == 0) { //not a 68020?
        snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68010");
        hw_info.cpu_type = CPU_68010;
    }

    else {// we detect now 68030 and 68040 manually , because Kick 1.3 does not know about a 68030+
        ULONG oldBits, newBits;

        // now we have at least a 68020/030
        // the CACRF_FreezeI is a 68020/030-only flag!
        oldBits = CacheControl(CACRF_FreezeI, CACRF_FreezeI); // can instruction cache be frezed?
        newBits = CacheControl(0, 0);
        CacheControl(oldBits & CACRF_FreezeI, CACRF_FreezeI); // reset to old state
        if ((newBits & CACRF_FreezeI) == CACRF_FreezeI)
        {
            oldBits = CacheControl(CACRF_IBE, CACRF_IBE); // can instruction burst be enabled?
            newBits = CacheControl(0, 0);
            CacheControl(oldBits & CACRF_IBE, CACRF_IBE); // reset to old state
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
            // now it's 68040/060 and it's derivatives
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
            else
            {
                snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), get_string(MSG_UNKNOWN));
                hw_info.cpu_type = CPU_UNKNOWN;
            }
        }
    }

    /* Get CPU revision from identify.library (returns string) */
    if ( hw_info.cpu_type == CPU_68060 ||
        hw_info.cpu_type == CPU_68LC060 ||
        hw_info.cpu_type == CPU_68EC060
    ) {
        hw_info.cpu_rev = detect_cpu_rev();
        snprintf(hw_info.cpu_revision, sizeof(hw_info.cpu_revision), "Rev. %d", hw_info.cpu_rev);
    }
    else {
        hw_info.cpu_rev = -1;
        snprintf(hw_info.cpu_revision, sizeof(hw_info.cpu_revision), get_string(MSG_NA));
    }
}

UWORD detect_cpu_rev(void)
{
    ULONG cpuReg = GetCPUReg();
    cpuReg = cpuReg>>8;
    cpuReg &= 0xFF;
    return (UWORD) cpuReg;
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

    //is there any fpu?
    if ((attnFlags & ((UWORD)AFF_68881|(UWORD)AFF_FPU40)) == 0) { //No FPU
        strncpy(hw_info.fpu_string, get_string(MSG_NONE),
                    sizeof(hw_info.fpu_string) - 1);
        hw_info.fpu_type = FPU_NONE;
        if (hw_info.cpu_type == CPU_68040) {
            hw_info.cpu_type = CPU_68LC040; //fpu-less cpu
            snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "68LC040");
        }
        //the 68060 is distinguished from the 68LC/EC060 by the cpu-id reg from the cpu-detect-function
        if (hw_info.cpu_type == CPU_68060) {
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68060 (%s)", get_string(MSG_OFF));
        }
        return;
    }

    //kick 1.3 does not know any better fpu than 68881!
    if ((attnFlags & (UWORD)AFF_68881) > 0) { //68881
        if ((attnFlags & (UWORD)AFF_68882) > 0) { //68881
            hw_info.fpu_type = FPU_68882;
            snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "68882");
        }
        else {
            hw_info.fpu_type = FPU_68881;
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
    strncpy(hw_info.mmu_string, get_string(MSG_NA), sizeof(hw_info.mmu_string) - 1);

    // first: try mmu.lib
    if ((DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37L)))
    {
        if ((MMUBase = OpenLibrary((CONST_STRPTR)"mmu.library", 40L)))
        { // check for mmu.lib
            fallBack = FALSE;
            switch (GetMMUType())
            {
            case MUTYPE_68851:
                hw_info.mmu_type = MMU_68851;
                strncpy(hw_info.mmu_string, "68851", sizeof(hw_info.mmu_string) - 1);
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68030:
                hw_info.mmu_type = MMU_68030;
                strncpy(hw_info.mmu_string, "68030", sizeof(hw_info.mmu_string) - 1);
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68040:
                hw_info.mmu_type = MMU_68040;
                strncpy(hw_info.mmu_string, "68040", sizeof(hw_info.mmu_string) - 1);
                hw_info.mmu_enabled = TRUE;
                break;
            case MUTYPE_68060:
                hw_info.mmu_type = MMU_68060;
                strncpy(hw_info.mmu_string, "68060", sizeof(hw_info.mmu_string) - 1);
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
                default:
                    break;
                }
                hw_info.mmu_type = MMU_NONE;
                strncpy(hw_info.mmu_string, get_string(MSG_NA), sizeof(hw_info.mmu_string) - 1);
                break;
            default:
                break;
            }
            CloseLibrary((struct Library *)MMUBase);
        }
        CloseLibrary((struct Library *)DOSBase);
    }


    if (fallBack) //mmu.library or dos.library didn't open
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
        default:
            cpuType = 0;
            break;
        }


        if (cpuType >= ASM_CPU_68020)
        {
            mmuResult = GetMMU(cpuType);
        }

        if (mmuResult > 0)
        {
            // we have an mmu!
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
            default:
                strncpy(hw_info.mmu_string, get_string(MSG_UNKNOWN), sizeof(hw_info.mmu_string) - 1);
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

/*
 * Detect chipset (Agnus/Denise)
 */
void detect_chipset(void)
{
    UWORD tmp, i;
    debug("    chipset: Detecting Paula...\n");

    /*Get Paula revision*/
    hw_info.paula_rev = *((volatile UWORD *)(CUSTOM_PAULA_ID));
    hw_info.paula_rev &= 0x00FE; //mask irelevant bits
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
            case 0x0: //OCS PAL
                //get possible mirrored version (A1000)
                tmp = *((volatile UWORD *)(CUSTOM_AGNUS_ID_MIRR));
                tmp &= 0X7F00; //mask Bit 14-8
                tmp = (tmp>>8); //shift to lower byte
                if (hw_info.agnus_rev == tmp)
                    hw_info.agnus_type = AGNUS_OCS_PAL;
                else
                    hw_info.agnus_type = AGNUS_OCS_FAT_PAL;
                hw_info.max_chip_ram = 512 * 1024;  /* 512K */
                break;
            case 0x10: //OCS NTSC
                //get possible mirrored version (A1000)
                tmp = *((volatile UWORD *)(CUSTOM_AGNUS_ID_MIRR));
                tmp &= 0X7F00; //mask Bit 14-8
                tmp = (tmp>>8); //shift to lower byte
                if (hw_info.agnus_rev == tmp)
                    hw_info.agnus_type = AGNUS_OCS_NTSC;
                else
                    hw_info.agnus_type = AGNUS_OCS_FAT_NTSC;
                hw_info.max_chip_ram = 512 * 1024;  /* 512K */
                break;
            case 0x20: //ECS PAL
                hw_info.agnus_type = AGNUS_ECS_PAL;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x30: //ECS NTSC
                hw_info.agnus_type = AGNUS_ECS_NTSC;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x21: //ALICE PAL
            case 0x22: //ALICE PAL
            case 0x23: //ALICE PAL
            case 0x24: //ALICE PAL
                hw_info.agnus_type = AGNUS_ALICE_PAL;
                hw_info.max_chip_ram = 2048 * 1024;  /* 2MB */
                break;
            case 0x31: //ALICE NTSC
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
            strncpy(hw_info.clock_string, get_string(MSG_MSM6242B),
                        sizeof(hw_info.clock_string) - 1);
            return;
        }
        if (val > 0) { //when val is not 0 we have no RTC!
            hw_info.clock_type = CLOCK_NONE;
            strncpy(hw_info.clock_string, get_string(MSG_CLOCK_NOT_FOUND),
                    sizeof(hw_info.clock_string) - 1);
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
            if (val == 0) { // comming closer: now read register A, which should be '0001!
                val = *((volatile unsigned char *)(RTC_BASE+RTC_REG_A));
                val &= RTC_MASK;
                if (val ==1 ) { // bingo! RP5C01A
                    hw_info.clock_type = CLOCK_RP5C01;
                    strncpy(hw_info.clock_string, get_string(MSG_RP5C01A),
                        sizeof(hw_info.clock_string) - 1);
                    return;
                }
            }
        }
    }
    // if we drop here, we found no clock
    hw_info.clock_type = CLOCK_NONE;
    strncpy(hw_info.clock_string, get_string(MSG_CLOCK_NOT_FOUND),
            sizeof(hw_info.clock_string) - 1);
    return;
}

/*
 * Detect if NV-Ram (batt Mem is available and read values)
 * Call after detect_clock and detect_chips!
 */
void detect_batt_mem(void)
{
    hw_info.battMemData.valid_data = FALSE;

    if ( hw_info.clock_type == CLOCK_RP5C01 //clock with NV-ram
        && hw_info.ramsey_rev > 0 //we have a ramsey (and might be a A3000
        && openBattMem() //batt mem ressource is open
        ) {
        hw_info.battMemData.valid_data = readBattMem(&hw_info.battMemData);
    }

}

/*
 * Detect Ramsey
 */
void detect_ramsey(void)
{

    /* Ramsey (A3000/A4000 RAM controller) */

    if (hw_info.gary_type != FAT_GARY) { // no fat gary -> no ramsey!
        hw_info.ramsey_rev = 0;
        return;
    }

    hw_info.ramsey_rev = GetRamseyRev(); //In A4000: must be done in supervisore mode!
    if ( hw_info.ramsey_rev == 0xFF ||  // unlikely!
        hw_info.ramsey_rev == 0x00 ) // unlikely!
    {
        hw_info.ramsey_rev = 0;
    }else {
        hw_info.ramsey_ctl = GetRamseyCtrl(); //In A4000: must be done in supervisore mode!

        hw_info.ramsey_page_enabled = hw_info.ramsey_ctl & RAMSEY_PAGE_MODE;
        hw_info.ramsey_burst_enabled = hw_info.ramsey_ctl & RAMSEY_BURST_MODE;
        hw_info.ramsey_wrap_enabled = hw_info.ramsey_ctl & RAMSEY_WRAP_MODE;
        hw_info.ramsey_size_1M = hw_info.ramsey_ctl & RAMSEY_SIZE;
        hw_info.ramsey_skip_enabled = hw_info.ramsey_ctl & RAMSEY_SKIP_MODE;
        hw_info.ramsey_refresh_rate = (hw_info.ramsey_ctl & RAMSEY_REFRESH_MODE)>>4;
    }
}

/*
 * Detect SDMAC
 */
/* Returns 2 for SDMAC-02, 4 for SDMAC-04/ReSDMAC, 0 if not present/detection fails */
void detect_sdmac(void)
{
    unsigned char sdmac_rev;
    uint32_t ovalue, rvalue, wvalue;
    uint8_t sdmac_version = 0;
    uint8_t istr;
    int pass;
    hw_info.sdmac_rev = 0;
    hw_info.is_A4000T = FALSE;
    *((volatile unsigned char *)FAT_GARY_TIME_OUT_REG) = FAT_GARY_TIME_OUT_DSACK;

    if (hw_info.gary_type == FAT_GARY)
    { // you need fat gary to access ncr!
        // now test for A4000T NCR53C710: upper four bits of CTEST8-register contains the chip-rev.
        sdmac_rev = *((volatile unsigned char *)(NCR_CTEST8_REG));
        sdmac_rev = (sdmac_rev & 0xF0) >> 4; // only upper four bits matter
        if (sdmac_rev != 0 && sdmac_rev != 0xF)
        {
            hw_info.sdmac_rev = sdmac_rev;
            hw_info.is_A4000T = TRUE;
        }
    }

    if (hw_info.ramsey_rev>0 && hw_info.gary_type == FAT_GARY && !hw_info.is_A4000T) { //you need fat gary and ramsey to access sdmac!
        //Switch to DSACK-Timeout to avoid bus erros on a A4000!
        sdmac_rev = *((volatile unsigned char *)(SDMAC_REVISION)); //this works only on resdmac
        if (sdmac_rev != 0 && sdmac_rev <= 0xF0) { //realistic values!
            hw_info.sdmac_rev = sdmac_rev;
        }
        if (hw_info.sdmac_rev == 0) {
            /* Quick check: ISTR bits - FIFO cannot be both empty and full */
            istr = *SDMAC_ISTR;
            if (istr == 0xff)
                return;
            if ((istr & SDMAC_ISTR_FIFOE) && (istr & SDMAC_ISTR_FIFOF))
                return;
            sdmac_version = 2; //default version
            /* Probe WTC registers to distinguish SDMAC-02 from SDMAC-04 */
            for (pass = 0; pass < 6 && sdmac_version > 0; pass++) {
                switch (pass) {
                    case 0: wvalue = 0x00000000; break;
                    case 1: wvalue = 0xffffffff; break;
                    case 2: wvalue = 0xa5a5a5a5; break;
                    case 3: wvalue = 0x5a5a5a5a; break;
                    case 4: wvalue = 0xc2c2c3c3; break;
                    case 5: wvalue = 0x3c3c3c3c; break;
                }

                ovalue = *SDMAC_WTC_ALT;
                *((volatile unsigned char *)RAMSEY_VER) = 0;/* Push write to bus */
                *SDMAC_WTC_ALT = wvalue;
                *((volatile unsigned char *)RAMSEY_VER) = 0;/* Push write to bus */
                rvalue = *SDMAC_WTC;
                *((volatile unsigned char *)RAMSEY_VER) = 0;/* Push write to bus */
                *SDMAC_WTC_ALT = ovalue;
                *((volatile unsigned char *)RAMSEY_VER) = 0;/* Push write to bus */

                if (rvalue == wvalue) {
                    if ((wvalue != 0x00000000) && (wvalue != 0xffffffff)) {
                        sdmac_version = 0; /* Detection failed */
                    }
                } else if (((rvalue ^ wvalue) & 0x00ffffff) == 0) {
                    /* SDMAC-02: only upper byte differs */
                    hw_info.sdmac_rev = 2;
                    return;
                } else if ((rvalue & (1 << 2)) == 0) {
                    /* SDMAC-04: bit 2 is always 0 */
                    if ((wvalue & (1 << 2)) > 0)
                        sdmac_version = 4;
                }
                else {
                    sdmac_version = 0; /* Detection failed */
                }
            }
            hw_info.sdmac_rev = sdmac_version;
        }
    }
    //switch back berr-timeout
    *((volatile unsigned char *)FAT_GARY_TIME_OUT_REG) = FAT_GARY_TIME_OUT_BERR;
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
        A save register to read is DMACONR. If this register is mirrored A1000 is there.

        A600/A1200 have an undocumented revision register at DE1000, but the 8-bit code is
        "morsed" only via the highest byte

        If this is "FF" or unstable, a A500/2000 is present.
    */
    hw_info.gary_type = GARY_UNKNOWN;

    /*
    A3000(T/+), A4000(T)
    We have to do this first, because a A3000 with a 67040 crashes on the A1000-tests due to mmu-restrictions
    Test, if we have the Power-Up-Register (A3000/4000).
    This writes a 0x80 and a zero to DMACONR on a A1000, which should be save
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
            //restore old value
            *((volatile unsigned char *)FAT_GARY_POWER_REG) = val;
            return;
        }
    }


    //test for mirroring (A1000/ A2000BSW)
    testVal1 = *((volatile UWORD *)(CUSTOM_JOY0DAT));
    testVal2 = *((volatile UWORD *)(CUSTOM_JOY1DAT)); //avoid bus stickyness (A3000)
    testVal2 = *((volatile UWORD *)(CUSTOM_JOY0DAT_MIRR));
    if (testVal1 == testVal2) {
        //do another test to be save
        testVal1 = *((volatile UWORD *)(CUSTOM_JOY1DAT));
        testVal2 = *((volatile UWORD *)(CUSTOM_JOY0DAT)); //avoid bus stickyness (A3000)
        testVal2 = *((volatile UWORD *)(CUSTOM_JOY1DAT_MIRR));
        if (testVal1 == testVal2) {
            hw_info.gary_type = GARY_A1000;
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
    if (OpenResource((CONST_STRPTR)"card.resource") != NULL) {
        hw_info.has_pcmcia = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_SLOT_PCMCIA));
        return;
    }

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
        return;
    }

    if (hw_info.gary_type == GAYLE) {
        /* A600 A1200 have a Gayle chip with ID*/
        hw_info.has_pcmcia = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                "%s", get_string(MSG_SLOT_PCMCIA));
        return;
    }

    /* A500/A1000/A2000/CDTV*/
    hw_info.has_zorro_slots = TRUE;
    snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
             "%s", get_string(MSG_ZORRO_II));
    return;
}

/*
 * Detect screen frequencies
 */
void detect_frequencies(void)
{
    /* PAL/NTSC detection */
    hw_info.is_pal = (GfxBase->DisplayFlags & PAL) ? TRUE : FALSE;

    if (hw_info.is_pal) {
        hw_info.horiz_freq = 15625;     /* 15.625 kHz */
        hw_info.vert_freq = 50;
        hw_info.supply_freq = 50;
        strncpy(hw_info.mode_string, get_string(MSG_MODE_PAL), sizeof(hw_info.mode_string) - 1);
    } else {
        hw_info.horiz_freq = 15734;     /* 15.734 kHz */
        hw_info.vert_freq = 60;
        hw_info.supply_freq = 60;
        strncpy(hw_info.mode_string, get_string(MSG_MODE_NTSC), sizeof(hw_info.mode_string) - 1);
    }

    /* EClock frequency from exec */
    hw_info.eclock_freq = SysBase->ex_EClockFrequency;
}

/*
 * Refresh cache status from current CACR
 */
void refresh_cache_status(void)
{
    ULONG cacr_bits;

    /* Determine available cache features based on CPU type */
    hw_info.has_icache = (hw_info.cpu_type >= CPU_68020);
    hw_info.has_dcache = (hw_info.cpu_type >= CPU_68030);
    hw_info.has_iburst = (hw_info.cpu_type >= CPU_68030);
    hw_info.has_dburst = (hw_info.cpu_type >= CPU_68030);
    hw_info.has_copyback = (hw_info.cpu_type >= CPU_68040 &&
                            hw_info.cpu_type != CPU_68LC040);

    /* Get current cache state */
    cacr_bits = CacheControl(0, 0);

    hw_info.icache_enabled = (cacr_bits & CACRF_EnableI) ? TRUE : FALSE;
    hw_info.dcache_enabled = (cacr_bits & CACRF_EnableD) ? TRUE : FALSE;
    hw_info.iburst_enabled = (cacr_bits & CACRF_IBE) ? TRUE : FALSE;
    hw_info.dburst_enabled = (cacr_bits & CACRF_DBE) ? TRUE : FALSE;
    hw_info.copyback_enabled = (cacr_bits & CACRF_CopyBack) ? TRUE : FALSE;
}

/*
 * Generate a comment based on system configuration
 */
void generate_comment(void)
{
    const char *comment;

    if (hw_info.cpu_type >= CPU_68060 && hw_info.cpu_mhz >= 5000) {
        comment = get_string(MSG_COMMENT_BLAZING);
    } else if (hw_info.cpu_type >= CPU_68060 && hw_info.cpu_mhz >= 2500) {
        comment = get_string(MSG_COMMENT_VERY_FAST);
    } else if (hw_info.cpu_type >= CPU_68040 && hw_info.cpu_mhz >= 2500) {
        comment = get_string(MSG_COMMENT_VERY_FAST);
    } else if (hw_info.cpu_type >= CPU_68030 && hw_info.cpu_mhz >= 2500) {
        comment = get_string(MSG_COMMENT_FAST);
    } else if (hw_info.cpu_type >= CPU_68020 && hw_info.cpu_mhz >= 1400) {
        comment = get_string(MSG_COMMENT_GOOD);
    } else if (hw_info.cpu_type <= CPU_68010) {
        comment = get_string(MSG_COMMENT_CLASSIC);
    } else {
        comment = get_string(MSG_COMMENT_DEFAULT);
    }

    strncpy(hw_info.comment, comment, sizeof(hw_info.comment) - 1);
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
