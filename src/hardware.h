// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Hardware detection header
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include "xsysinfo.h"

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
    CPU_68060,
    CPU_68EC060,
    CPU_68LC060,
    CPU_UNKNOWN
} CPUType;

/* FPU types */
typedef enum {
    FPU_NONE,
    FPU_68881,
    FPU_68882,
    FPU_68040,
    FPU_68060,
    FPU_UNKNOWN
} FPUType;

/* MMU types */
typedef enum {
    MMU_NONE,
    MMU_68851,
    MMU_68030,
    MMU_68040,
    MMU_68060,
    MMU_UNKNOWN
} MMUType;

/* Agnus/Alice types */
typedef enum {
    AGNUS_UNKNOWN,
    AGNUS_OCS_NTSC,
    AGNUS_OCS_PAL,
    AGNUS_ECS_NTSC,
    AGNUS_ECS_PAL,
    AGNUS_ECS_NTSC_1MB,
    AGNUS_ECS_PAL_1MB,
    AGNUS_ECS_NTSC_2MB,
    AGNUS_ECS_PAL_2MB,
    AGNUS_ALICE_NTSC,
    AGNUS_ALICE_PAL,
    AGNUS_ALICE_NTSC_2MB,
    AGNUS_ALICE_PAL_2MB
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

/* Clock chip types */
typedef enum {
    CLOCK_NONE,
    CLOCK_RP5C01,       /* A4000 style (Ricoh) */
    CLOCK_MSM6242,      /* A1200 style (OKI) */
    CLOCK_RF5C01,
    CLOCK_UNKNOWN
} ClockType;

/* Hardware information structure */
typedef struct {
    /* CPU */
    CPUType cpu_type;
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
    BOOL icache_enabled;
    BOOL dcache_enabled;
    BOOL iburst_enabled;
    BOOL dburst_enabled;
    BOOL copyback_enabled;

    /* Chipset */
    AgnusType agnus_type;
    ULONG max_chip_ram;     /* In bytes */
    char agnus_string[32];

    DeniseType denise_type;
    char denise_string[32];

    /* Clock */
    ClockType clock_type;
    char clock_string[32];

    /* Other chips */
    ULONG ramsey_rev;       /* 0 = not present */
    ULONG gary_rev;         /* 0 = not present */

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
    ULONG kickstart_size;

} HardwareInfo;

/* Global hardware info (filled by detect_hardware) */
extern HardwareInfo hw_info;

/* Function prototypes */
BOOL detect_hardware(void);
void refresh_cache_status(void);

/* Individual detection functions */
void detect_cpu(void);
void detect_fpu(void);
void detect_mmu(void);
void detect_chipset(void);
void detect_clock(void);
void detect_system_chips(void);
void detect_frequencies(void);
void generate_comment(void);

/* CPU MHz measurement */
ULONG measure_cpu_frequency(void);

#endif /* HARDWARE_H */
