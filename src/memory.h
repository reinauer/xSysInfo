// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Memory information header
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "xsysinfo.h"

/* Maximum memory regions we'll track */
#define MAX_MEMORY_REGIONS  32

/* Memory region information */
typedef struct {
    APTR start_address;
    APTR end_address;
    ULONG total_size;
    UWORD mem_type;         /* MEMF_* flags */
    WORD priority;
    APTR lower_bound;
    APTR upper_bound;
    APTR first_free;
    ULONG amount_free;
    ULONG largest_block;
    ULONG num_chunks;
    char node_name[64];
    char type_string[64];   /* Human-readable type */
    ULONG speed_bytes_sec;  /* Read speed in bytes/second */
    BOOL speed_measured;    /* TRUE if speed test has been run */
} MemoryRegion;

/* Memory region list */
typedef struct {
    MemoryRegion regions[MAX_MEMORY_REGIONS];
    ULONG count;
} MemoryRegionList;

/* Global memory region list */
extern MemoryRegionList memory_regions;

/* Function prototypes */
void enumerate_memory_regions(void);
void refresh_memory_region(ULONG index);

/* Get memory type as string */
const char *get_memory_type_string(UWORD attrs, APTR addr);

/* Count free chunks and find largest block in a memory region */
void analyze_memory_region(struct MemHeader *mh, ULONG *chunks, ULONG *largest);

/* Measure memory read speed for a region (returns bytes/second) */
ULONG measure_memory_speed(ULONG index);

/* Draw memory view */
void draw_memory_view(void);

#endif /* MEMORY_H */
