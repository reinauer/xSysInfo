// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Hardware detection header
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include "xsysinfo.h"
#include "battmem.h"

#define KICK_SIZE    0xF80000
#define KICK_VERSION    0xF8000C
#define KICK_VERSION_MIRR    0x10F8000C
#define KICK_REVISION    0xF8000E
#define RAMSEY_VER     0x00DE0043 // Ramsey version register
#define RAMSEY_CTRL    0x00DE0003 // Ramsey control register
#define SDMAC_REVISION 0x00DD0020 // Read   Revision of ReSDMAC
#define RAMSEY_PAGE_MODE 0x1
#define RAMSEY_BURST_MODE 0x2
#define RAMSEY_WRAP_MODE 0x4
#define RAMSEY_SIZE 0x8
#define RAMSEY_SKIP_MODE 0x10
#define RAMSEY_REFRESH_MODE 0x20
#define RTC_BASE   0xDC0000
#define RTC_REG_A    0x2B
#define RTC_REG_C   0x33
#define RTC_REG_D   0x37
#define RTC_REG_F   0x3F
#define RTC_MASK   0xF //only the lower 4 bit matter!
#define CUSTOM_BLTDDAT   0xDFF000
#define CUSTOM_DMACONR   0xDFF002
#define CUSTOM_DMACONR_MIRR   0xDAF002
#define CUSTOM_JOY0DAT   0xDFF00A
#define CUSTOM_JOY0DAT_MIRR  0xDAF00A
#define CUSTOM_JOY1DAT   0xDFF00C
#define CUSTOM_JOY1DAT_MIRR  0xDAF00C
#define CUSTOM_PAULA_ID  0xDFF016
#define CUSTOM_DENISE_ID  0xDFF07C
#define CUSTOM_VPOSR    0xDFF004
#define CUSTOM_AGNUS_ID CUSTOM_VPOSR
#define CUSTOM_AGNUS_ID_MIRR 0xDCF004
#define GAYLE_ID 0xDE1000
#define FAT_GARY_POWER_REG 0xDE0002
#define FAT_GARY_POWER_CYCLE 0x80
#define FAT_GARY_POWER_GOOD 0x0
#define FAT_GARY_TIME_OUT_REG 0xDE0000
#define FAT_GARY_TIME_OUT_DSACK 0x0
#define FAT_GARY_TIME_OUT_BERR 0x80
#define SDMAC_ISTR      ((volatile uint8_t *)0xDD001F)
#define SDMAC_WTC       ((volatile uint32_t *)0xDD0024)
#define SDMAC_WTC_ALT   ((volatile uint32_t *)0xDD0028)
#define NCR_CTEST8_REG 0x00DD0061

#ifndef AFB_68080
/*
 * The AFB_68080 bit is set when a working AC68080
 * is in the system. If this is set then all bits
 * for 010/020/030/040 are also set, since the 080
 * is intended to be compatible with all of them.
 */
#define AFB_68080	10
#define AFF_68080	(1<<10)
#endif

/* ISTR bits */
#define SDMAC_ISTR_FIFOE  0x01
#define SDMAC_ISTR_FIFOF  0x02

/* CPU types */
typedef enum {
    CPU_68000,
    CPU_68010,
    CPU_68020,
    CPU_68EC020,
    CPU_68030,
    CPU_68EC030,
    CPU_68040,
    CPU_68LC040,
    CPU_68EC040,
    CPU_68060,
    CPU_68EC060,
    CPU_68LC060,
    CPU_68080,
    CPU_EMU,
    CPU_UNKNOWN
} CPUType;

/* FPU types */
typedef enum {
    FPU_NONE,
    FPU_68881,
    FPU_68882,
    FPU_68040,
    FPU_68060,
    FPU_68080,
    FPU_UNKNOWN
} FPUType;

/* MMU types */
typedef enum {
    MMU_NONE,
    MMU_68851,
    MMU_68030,
    MMU_68040,
    MMU_68060,
    MMU_68080,
    MMU_UNKNOWN
} MMUType;

/* Agnus/Alice types */
typedef enum {
    AGNUS_UNKNOWN,
    AGNUS_OCS_NTSC,
    AGNUS_OCS_PAL,
    AGNUS_OCS_FAT_NTSC,
    AGNUS_OCS_FAT_PAL,
    AGNUS_ECS_NTSC,
    AGNUS_ECS_PAL,
    AGNUS_ALICE_NTSC,
    AGNUS_ALICE_PAL,
    AGNUS_SAGA
} AgnusType;

/* Denise/Lisa types */
typedef enum {
    DENISE_UNKNOWN,
    DENISE_OCS,
    DENISE_ECS,
    DENISE_LISA,
    DENISE_ISABEL,
    DENISE_MONICA
} DeniseType;

/* Denise/Lisa types */
typedef enum {
    PAULA_UNKNOWN,
    PAULA_ORIG,
    PAULA_SAGA
} PaulaType;


/* Clock chip types */
typedef enum {
    CLOCK_NONE,
    CLOCK_RP5C01,       /* A4000 style (Ricoh) */
    CLOCK_MSM6242,      /* A1200 style (OKI) */
    CLOCK_RF5C01,
    CLOCK_UNKNOWN
} ClockType;

/*Gayle/Gary types*/
typedef enum{
    GARY_UNKNOWN,
    GARY_A1000,
    GARY_A500,
    GAYLE,
    FAT_GARY
}GaryType;

/* Hardware information structure */
typedef struct {
    /* CPU */
    CPUType cpu_type;
    UWORD cpu_rev;
    char cpu_revision[16];
    ULONG cpu_mhz;          /* CPU MHz * 100 */
    char cpu_string[32];

    /* FPU */
    FPUType fpu_type;
    ULONG fpu_mhz;          /* FPU MHz * 100 */
    char fpu_string[32];

    /* MMU */
    MMUType mmu_type;
    BOOL mmu_enabled;
    char mmu_string[32];

    /* VBR */
    ULONG vbr;

    /* Cache status */
    BOOL has_icache;
    BOOL has_dcache;
    BOOL has_iburst;
    BOOL has_dburst;
    BOOL has_copyback;
    BOOL has_super_scalar;
    BOOL icache_enabled;
    BOOL dcache_enabled;
    BOOL iburst_enabled;
    BOOL dburst_enabled;
    BOOL copyback_enabled;
    BOOL super_scalar_enabled;

    /* Chipset */
    AgnusType agnus_type;
    UWORD agnus_rev;
    ULONG max_chip_ram;     /* In bytes */

    DeniseType denise_type;
    UWORD denise_rev;

    PaulaType paula_type;
    UWORD paula_rev;

    /* Clock */
    ClockType clock_type;
    char clock_string[32];

    /* Other chips */
    unsigned char gary_rev;         /* 0 = not present */
    unsigned char ramsey_rev;       /* 0 = not present */
    unsigned char ramsey_ctl;
    unsigned char sdmac_rev;         /* 0 = not present */
    GaryType gary_type;

    BOOL is_A4000T;

    BOOL ramsey_page_enabled;
    BOOL ramsey_burst_enabled;
    BOOL ramsey_wrap_enabled;
    BOOL ramsey_size_1M;
    BOOL ramsey_skip_enabled;
    ULONG ramsey_refresh_rate;     /* 0 = ???, 1 = ???, 2 = ???, 3 = ??? */

    /* BattMemRessources (if available)*/
    BattMemData battMemData;

    /* System info */
    BOOL has_zorro_slots;
    BOOL has_pcmcia;
    char card_slot_string[32];

    /* Screen frequencies */
    ULONG horiz_freq;       /* Hz */
    ULONG vert_freq;        /* Hz */
    ULONG eclock_freq;      /* Hz */
    ULONG supply_freq;      /* Hz (50 or 60) */

    /* Video mode */
    BOOL is_pal;
    char mode_string[16];

    /* Comment */
    char comment[64];

    /* Kickstart info */
    UWORD kickstart_version;
    UWORD kickstart_revision;
    UWORD kickstart_patch_version;
    UWORD kickstart_patch_revision;
    ULONG kickstart_size;

} HardwareInfo;

/* Global hardware info (filled by detect_hardware) */
extern HardwareInfo hw_info;

/* Function prototypes */
BOOL detect_hardware(void);
void refresh_cache_status(void);

/* Individual detection functions */
BOOL detect_emu68_systems(void);
void detect_cpu(void);
void detect_fpu(void);
void detect_mmu(void);
void read_vbr(void);
void detect_chipset(void);
void detect_clock(void);
void detect_batt_mem(void);
void detect_gary(void);
void detect_ramsey(void);
void detect_sdmac(void);
void detect_system_chips(void);
void detect_frequencies(void);
void generate_comment(void);
void detect_kickstart(void);
UWORD detect_cpu_rev(void);
BOOL get_super_scalar_mode(void);
BOOL set_super_scalar_mode(BOOL value);

/* CPU MHz measurement */
ULONG measure_cpu_frequency(void);

#endif /* HARDWARE_H */
