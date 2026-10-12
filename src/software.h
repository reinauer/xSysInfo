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
    /* A line built from a catalog string: its id plus one (0 for none) and
     * number, so a report can build it again in English. */
    UWORD text_id;
    ULONG text_arg;
} SoftwareEntry;

/* Software list, allocated to fit its entries */
typedef struct {
    SoftwareEntry *entries;
    ULONG count;
    ULONG capacity;
} SoftwareList;

/* System software collected once alongside the software lists. */
typedef struct {
    ULONG os_id;               /* IDOS_*; zero when unknown */
    char os_name[48];
    ULONG graphics_system_id;  /* IDGOS_* */
    char graphics_system[32];
    BOOL has_workbench_version;
    UWORD workbench_version;
    UWORD workbench_revision;
    BOOL has_setpatch_version;
    UWORD setpatch_version;
    UWORD setpatch_revision;
    BOOL is_tinysetpatch;      /* TinySetPatch's persistent marker is present */
    UWORD tinysetpatch_version;
    UWORD tinysetpatch_revision;
} SystemSoftwareInfo;

extern SystemSoftwareInfo system_software;

/* Global software lists */
extern SoftwareList libraries_list;
extern SoftwareList devices_list;
extern SoftwareList resources_list;
extern SoftwareList mmu_list;

/* Function prototypes */
void enumerate_libraries(void);
void enumerate_devices(void);
void enumerate_resources(void);
void enumerate_mmu_entries(void);
void free_software_lists(void);
void enumerate_all_software(void);
void detect_system_software(void);

/* Get the current list based on type */
SoftwareList *get_software_list(SoftwareType type);

/* Sort entries alphabetically by name */
void sort_software_list(SoftwareList *list);

/* The entry's text in the current language; see SoftwareEntry.text_id. */
const char *software_entry_text(const SoftwareEntry *entry, char *buffer,
                                size_t size);

#endif /* SOFTWARE_H */
