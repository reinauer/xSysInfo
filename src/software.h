// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - System software enumeration header
 */

#ifndef SOFTWARE_H
#define SOFTWARE_H

#include "xsysinfo.h"

/* Maximum entries we'll track */
#define MAX_SOFTWARE_ENTRIES    256

/* Software entry */
typedef struct {
    char name[64];
    MemoryLocation location;
    APTR address;
    UWORD version;
    UWORD revision;
} SoftwareEntry;

/* Software list */
typedef struct {
    SoftwareEntry entries[MAX_SOFTWARE_ENTRIES];
    ULONG count;
} SoftwareList;

/* Global software lists */
extern SoftwareList libraries_list;
extern SoftwareList devices_list;
extern SoftwareList resources_list;

/* Function prototypes */
void enumerate_libraries(void);
void enumerate_devices(void);
void enumerate_resources(void);
void enumerate_mmu_entries(void);
void enumerate_all_software(void);

/* Get the current list based on type */
SoftwareList *get_software_list(SoftwareType type);

/* Sort entries alphabetically by name */
void sort_software_list(SoftwareList *list);

#endif /* SOFTWARE_H */
