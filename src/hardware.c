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
#include <libraries/identify.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <proto/identify.h>

#include "xsysinfo.h"
#include "hardware.h"
#include "locale_str.h"
#include "debug.h"

/* Global hardware info */
HardwareInfo hw_info;

/* External library bases */
extern struct ExecBase *SysBase;
extern struct GfxBase *GfxBase;
extern struct Library *IdentifyBase;

/* String buffer for identify.library results */
static char id_buffer[256];

/* Helper to safely get string from identify.library */
static void get_hardware_string(ULONG type, char *buffer, size_t size)
{
    STRPTR result = IdHardware(type, NULL);
    if (result) {
        strncpy(buffer, (const char *)result, size - 1);
        buffer[size - 1] = '\0';
    } else {
        buffer[0] = '\0';
    }
}

/*
 * Main hardware detection function
 */
BOOL detect_hardware(void)
{
    memset(&hw_info, 0, sizeof(HardwareInfo));

    debug("  hw: Detecting CPU...\n");
    detect_cpu();
    debug("  hw: Detecting FPU...\n");
    detect_fpu();
    debug("  hw: Detecting MMU...\n");
    detect_mmu();
    debug("  hw: Detecting chipset...\n");
    detect_chipset();
    debug("  hw: Detecting clock...\n");
    detect_clock();
    debug("  hw: Detecting system chips...\n");
    detect_system_chips();
    debug("  hw: Detecting frequencies...\n");
    detect_frequencies();
    debug("  hw: Refreshing cache status...\n");
    refresh_cache_status();
    debug("  hw: Generating comment...\n");
    generate_comment();

    /* Get Kickstart info from identify.library */
    /* Get ROM version string and parse it (format: "Vxx.yy" or "xx.yy") */
    {
        STRPTR rom_ver = IdHardware(IDHW_ROMVER, NULL);
        if (rom_ver) {
            const char *p = (const char *)rom_ver;
            /* Skip 'V' prefix if present */
            if (*p == 'V' || *p == 'v') p++;
            /* Parse version.revision */
            hw_info.kickstart_version = 0;
            hw_info.kickstart_revision = 0;
            while (*p >= '0' && *p <= '9') {
                hw_info.kickstart_version = hw_info.kickstart_version * 10 + (*p - '0');
                p++;
            }
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') {
                    hw_info.kickstart_revision = hw_info.kickstart_revision * 10 + (*p - '0');
                    p++;
                }
            }
        }
        /* Fallback to exec version if identify didn't provide ROM version */
        if (hw_info.kickstart_version == 0) {
            hw_info.kickstart_version = SysBase->LibNode.lib_Version;
            hw_info.kickstart_revision = SysBase->LibNode.lib_Revision;
        }
    }

    /* Get ROM size from identify.library */
    hw_info.kickstart_size = IdHardwareNum(IDHW_ROMSIZE, NULL);
    if (hw_info.kickstart_size == 0) {
        /* Fallback: default to 512K */
        hw_info.kickstart_size = 512;
    }

    debug("  hw: Hardware detection complete.\n");
    return TRUE;
}

/*
 * Detect CPU type and speed
 */
void detect_cpu(void)
{
    ULONG cpu_num;

    /* Get CPU string from identify.library */
    get_hardware_string(IDHW_CPU, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.cpu_string, sizeof(hw_info.cpu_string), "%s", id_buffer);

    /* Get numeric CPU type */
    cpu_num = IdHardwareNum(IDHW_CPU, NULL);

    switch (cpu_num) {
        case IDCPU_68000:
            hw_info.cpu_type = CPU_68000;
            break;
        case IDCPU_68010:
            hw_info.cpu_type = CPU_68010;
            break;
        case IDCPU_68020:
            hw_info.cpu_type = CPU_68020;
            break;
        /* IDCPU_68EC020 not defined in identify.h V45 */
        case IDCPU_68030:
            hw_info.cpu_type = CPU_68030;
            break;
        case IDCPU_68EC030:
            hw_info.cpu_type = CPU_68EC030;
            break;
        case IDCPU_68040:
            hw_info.cpu_type = CPU_68040;
            break;
        case IDCPU_68LC040:
            hw_info.cpu_type = CPU_68LC040;
            break;
        case IDCPU_68060:
            hw_info.cpu_type = CPU_68060;
            break;
        /* IDCPU_68EC060 not defined in identify.h V45 */
        case IDCPU_68LC060:
            hw_info.cpu_type = CPU_68LC060;
            break;
        default:
            hw_info.cpu_type = CPU_UNKNOWN;
            break;
    }

    /* Get CPU frequency from identify.library */
    hw_info.cpu_mhz = measure_cpu_frequency();

    /* Get CPU revision from identify.library (returns string) */
    get_hardware_string(IDHW_CPUREV, hw_info.cpu_revision,
                        sizeof(hw_info.cpu_revision));
}

/*
 * Detect FPU type
 */
void detect_fpu(void)
{
    ULONG fpu_num;
    ULONG fpu_clock;

    /* Get FPU string from identify.library */
    get_hardware_string(IDHW_FPU, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.fpu_string, sizeof(hw_info.fpu_string), "%s", id_buffer);

    /* Get numeric FPU type */
    fpu_num = IdHardwareNum(IDHW_FPU, NULL);

    switch (fpu_num) {
        case IDFPU_NONE:
            hw_info.fpu_type = FPU_NONE;
            strncpy(hw_info.fpu_string, get_string(MSG_NONE),
                    sizeof(hw_info.fpu_string) - 1);
            break;
        case IDFPU_68881:
            hw_info.fpu_type = FPU_68881;
            break;
        case IDFPU_68882:
            hw_info.fpu_type = FPU_68882;
            break;
        case IDFPU_68040:
            hw_info.fpu_type = FPU_68040;
            break;
        case IDFPU_68060:
            hw_info.fpu_type = FPU_68060;
            break;
        default:
            hw_info.fpu_type = FPU_UNKNOWN;
            break;
    }

    /* Get FPU clock from identify.library */
    fpu_clock = IdHardwareNum(IDHW_FPUCLOCK, NULL);
    if (fpu_clock > 0 && fpu_clock < 1000) {
        hw_info.fpu_mhz = fpu_clock * 100;
    } else {
        hw_info.fpu_mhz = 0;
    }
}

/*
 * Detect MMU type
 */
void detect_mmu(void)
{
    ULONG mmu_num;

    /* Get MMU string from identify.library */
    get_hardware_string(IDHW_MMU, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.mmu_string, sizeof(hw_info.mmu_string), "%s", id_buffer);

    /* Get numeric MMU type */
    mmu_num = IdHardwareNum(IDHW_MMU, NULL);

    switch (mmu_num) {
        case IDMMU_NONE:
            hw_info.mmu_type = MMU_NONE;
            strncpy(hw_info.mmu_string, get_string(MSG_NA),
                    sizeof(hw_info.mmu_string) - 1);
            break;
        case IDMMU_68851:
            hw_info.mmu_type = MMU_68851;
            break;
        case IDMMU_68030:
            hw_info.mmu_type = MMU_68030;
            break;
        case IDMMU_68040:
            hw_info.mmu_type = MMU_68040;
            break;
        case IDMMU_68060:
            hw_info.mmu_type = MMU_68060;
            break;
        default:
            hw_info.mmu_type = MMU_UNKNOWN;
            break;
    }

    /* Check if MMU is in use (mmu.library loaded) */
    hw_info.mmu_enabled = FALSE;
    if (hw_info.mmu_type != MMU_NONE) {
        struct Library *mmulib = OpenLibrary((CONST_STRPTR)"mmu.library", 0);
        if (mmulib) {
            hw_info.mmu_enabled = TRUE;
            CloseLibrary(mmulib);
        }
    }

    /* Get VBR */
    get_hardware_string(IDHW_VBR, id_buffer, sizeof(id_buffer));
    hw_info.vbr = IdHardwareNum(IDHW_VBR, NULL);
}

/*
 * Detect chipset (Agnus/Denise)
 */
void detect_chipset(void)
{
    ULONG chipset, agnus_mode, denise_num;

    /* Get Agnus/Alice info */
    get_hardware_string(IDHW_AGNUS, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.agnus_string, sizeof(hw_info.agnus_string), "%s", id_buffer);

    chipset = IdHardwareNum(IDHW_CHIPSET, NULL);
    agnus_mode = IdHardwareNum(IDHW_AGNUSMODE, NULL);

    /* Determine Agnus type and max chip RAM */
    hw_info.max_chip_ram = 512 * 1024;  /* Default 512K */

    if (chipset == IDCS_AGA || chipset == IDCS_AAA || chipset == IDCS_SAGA) {
        /* AGA/Alice */
        hw_info.max_chip_ram = 2 * 1024 * 1024;
        if (agnus_mode == IDAM_PAL) {
            hw_info.agnus_type = AGNUS_ALICE_PAL;
        } else {
            hw_info.agnus_type = AGNUS_ALICE_NTSC;
        }
    } else if (chipset == IDCS_ECS || chipset == IDCS_NECS) {
        /* ECS */
        hw_info.max_chip_ram = 2 * 1024 * 1024; /* Can address up to 2MB, though usually 1MB */
        if (agnus_mode == IDAM_PAL) {
            hw_info.agnus_type = AGNUS_ECS_PAL;
        } else {
            hw_info.agnus_type = AGNUS_ECS_NTSC;
        }
    } else {
        /* OCS or Unknown */
        if (agnus_mode == IDAM_PAL) {
            hw_info.agnus_type = AGNUS_OCS_PAL;
        } else if (agnus_mode == IDAM_NTSC) {
            hw_info.agnus_type = AGNUS_OCS_NTSC;
        } else {
            hw_info.agnus_type = AGNUS_UNKNOWN;
        }
    }

    /* Get Denise/Lisa info */
    get_hardware_string(IDHW_DENISE, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.denise_string, sizeof(hw_info.denise_string), "%s", id_buffer);

    denise_num = IdHardwareNum(IDHW_DENISE, NULL);

    switch (denise_num) {
        case IDDN_NONE:
            hw_info.denise_type = DENISE_UNKNOWN;
            break;
        case IDDN_8362: /* OCS Denise */
        case IDDN_8369: /* Prototype */
            hw_info.denise_type = DENISE_OCS;
            break;
        case IDDN_8373: /* ECS Denise */
            hw_info.denise_type = DENISE_ECS;
            break;
        case IDDN_4203: /* Lisa */
            hw_info.denise_type = DENISE_LISA;
            break;
        case IDDN_ISABEL: /* SAGA/Vampire */
            hw_info.denise_type = DENISE_ISABEL;
            break;
        case IDDN_MONICA: /* AAA prototype */
            hw_info.denise_type = DENISE_MONICA;
            break;
        default:
            hw_info.denise_type = DENISE_UNKNOWN;
            break;
    }
}

/*
 * Detect RTC clock chip
 */
void detect_clock(void)
{
    ULONG clock_num;

    /* Get clock info from identify.library */
    get_hardware_string(IDHW_RTC, id_buffer, sizeof(id_buffer));
    snprintf(hw_info.clock_string, sizeof(hw_info.clock_string), "%s", id_buffer);

    clock_num = IdHardwareNum(IDHW_RTC, NULL);

    switch (clock_num) {
        case IDRTC_NONE:
            hw_info.clock_type = CLOCK_NONE;
            strncpy(hw_info.clock_string, get_string(MSG_CLOCK_NOT_FOUND),
                    sizeof(hw_info.clock_string) - 1);
            break;
        case IDRTC_RICOH: /* RP5C01A */
            hw_info.clock_type = CLOCK_RP5C01;
            break;
        case IDRTC_OKI: /* MSM6242B */
            hw_info.clock_type = CLOCK_MSM6242;
            break;
        default:
            hw_info.clock_type = CLOCK_UNKNOWN;
            strncpy(hw_info.clock_string, get_string(MSG_CLOCK_FOUND),
                    sizeof(hw_info.clock_string) - 1);
            break;
    }
}

/*
 * Detect Ramsey, Gary, and expansion slots
 */
void detect_system_chips(void)
{
    ULONG ramsey_num, gary_num;

    /* Ramsey (A3000/A4000 RAM controller) */
    ramsey_num = IdHardwareNum(IDHW_RAMSEY, NULL);
    if (ramsey_num != 0 && ramsey_num != IDRSY_NONE) {
        hw_info.ramsey_rev = ramsey_num;
    } else {
        hw_info.ramsey_rev = 0;
    }

    /* Gary (A3000/A4000 I/O controller) */
    gary_num = IdHardwareNum(IDHW_GARY, NULL);
    if (gary_num != 0 && gary_num != IDGRY_NONE) {
        hw_info.gary_rev = gary_num;
    } else {
        hw_info.gary_rev = 0;
    }

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
    if (hw_info.ramsey_rev != 0) {
        /* A3000/A4000 have Ramsey - these have Zorro III slots */
        hw_info.has_zorro_slots = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_ZORRO_III));
        return;
    }

    if (hw_info.gary_rev != 0) {
        /* A500/A2000 have Gary but no Ramsey - these have Zorro II slots */
        hw_info.has_zorro_slots = TRUE;
        snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                 "%s", get_string(MSG_ZORRO_II));
        return;
    }

    /* Fall back to system type detection for edge cases */
    {
        ULONG system_num = IdHardwareNum(IDHW_SYSTEM, NULL);

        switch (system_num) {
            case IDSYS_AMIGA600:
            case IDSYS_AMIGA1200:
                hw_info.has_pcmcia = TRUE;
                snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                         "%s", get_string(MSG_SLOT_PCMCIA));
                break;
            case IDSYS_AMIGA500:
            case IDSYS_AMIGA2000:
                hw_info.has_zorro_slots = TRUE;
                snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                         "%s", get_string(MSG_ZORRO_II));
                break;
            case IDSYS_AMIGA3000:
            case IDSYS_AMIGA4000:
                hw_info.has_zorro_slots = TRUE;
                snprintf(hw_info.card_slot_string, sizeof(hw_info.card_slot_string),
                         "%s", get_string(MSG_ZORRO_III));
                break;
            default:
                break;
        }
    }
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
    hw_info.has_dcache = (hw_info.cpu_type >= CPU_68030 &&
                          hw_info.cpu_type != CPU_68EC030);
    hw_info.has_iburst = (hw_info.cpu_type >= CPU_68030);
    hw_info.has_dburst = (hw_info.cpu_type >= CPU_68030 &&
                          hw_info.cpu_type != CPU_68EC030);
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
    /* Try to get from identify.library first */
    ULONG speed_mhz = IdHardwareNum(IDHW_CPUCLOCK, NULL);

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
            return 2500;  /* A4000 stock */
        case CPU_68060:
        case CPU_68EC060:
        case CPU_68LC060:
            return 5000;  /* Common 060 speed */
        default:
            return 709;
    }
}
