// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - System software enumeration (libraries, devices, resources)
 */

#include <string.h>

#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <exec/resident.h>

#include <proto/exec.h>
#include <proto/mmu.h>

#include "xsysinfo.h"
#include "software.h"
#include "hardware.h"
#include "locale_str.h"

/* Global software lists */
SoftwareList libraries_list;
SoftwareList devices_list;
SoftwareList resources_list;
SoftwareList mmu_list;

/* External references */
extern struct ExecBase *SysBase;

/* Global variables*/
BOOL mmuLoaded = FALSE;
extern struct Library *MMUBase;
extern struct DosLibrary *DOSBase;

/*
 * Copy name, stripping everything after the last dot
 * e.g. "exec.library" -> "exec", "a4092.device" -> "a4092"
 */
static void copy_base_name(char *dest, const char *src, size_t destsize)
{
    const char *dot;
    size_t len;

    if (!src || !dest || destsize == 0) return;

    /* Find last dot */
    dot = strrchr(src, '.');

    if (dot && dot > src) {
        /* Copy only up to the dot */
        len = (size_t)(dot - src);
        if (len >= destsize) len = destsize - 1;
        strncpy(dest, src, len);
        dest[len] = '\0';
    } else {
        /* No dot found, copy entire string */
        strncpy(dest, src, destsize - 1);
        dest[destsize - 1] = '\0';
    }
}

/* Comparison function for sorting */
static int compare_entries(const void *a, const void *b)
{
    const SoftwareEntry *ea = (const SoftwareEntry *)a;
    const SoftwareEntry *eb = (const SoftwareEntry *)b;
    return stricmp(ea->name, eb->name);
}

/*
 * Sort a software list alphabetically
 */
void sort_software_list(SoftwareList *list)
{
    if (list && list->count > 1) {
        /* Simple bubble sort - OK for small lists */
        ULONG i, j;
        for (i = 0; i < list->count - 1; i++) {
            for (j = 0; j < list->count - i - 1; j++) {
                if (compare_entries(&list->entries[j],
                                    &list->entries[j + 1]) > 0) {
                    SoftwareEntry temp = list->entries[j];
                    list->entries[j] = list->entries[j + 1];
                    list->entries[j + 1] = temp;
                }
            }
        }
    }
}

/*
 * Enumerate all open libraries
 */
void enumerate_libraries(void)
{
    struct Library *lib;
    ULONG i;
    SoftwareEntry *entry;

    memset(&libraries_list, 0, sizeof(libraries_list));

    Forbid();

    for (lib = (struct Library *)SysBase->LibList.lh_Head;
         (struct Node *)lib != (struct Node *)&SysBase->LibList.lh_Tail;
         lib = (struct Library *)lib->lib_Node.ln_Succ) {

        if (libraries_list.count >= MAX_SOFTWARE_ENTRIES) break;

        entry = &libraries_list.entries[libraries_list.count];

        if (lib->lib_Node.ln_Name) {
            copy_base_name(entry->name, lib->lib_Node.ln_Name, sizeof(entry->name));
            if (strcmp(lib->lib_Node.ln_Name,"mmu.library") == 0) {
                mmuLoaded = TRUE;
            }
        } else {
            strncpy(entry->name, "(unknown)", sizeof(entry->name) - 1);
        }

        entry->address = (APTR)lib;
        entry->version = lib->lib_Version;
        entry->revision = lib->lib_Revision;
        entry->location = determine_mem_location((APTR)lib);

        libraries_list.count++;
    }

    Permit();

    sort_software_list(&libraries_list);

    /* Insert artificial "kickstart" entry at the beginning */
    /* Insert artificial "kickstart (soft)" entry at the beginning */
    if ((libraries_list.count+1) < MAX_SOFTWARE_ENTRIES) {
        if ((hw_info.kickstart_version != hw_info.kickstart_patch_version ||
            hw_info.kickstart_revision != hw_info.kickstart_patch_revision) &&
            0 != hw_info.kickstart_patch_version &&
            0 != hw_info.kickstart_patch_revision &&
            hw_info.kickstart_version >= 40 //softkick is possible from Kick 3.1 (v40) and newer
        ) {
            /* Shift all entries by 1 position */
            for (i = libraries_list.count; i > 0; i--) {
                libraries_list.entries[i] = libraries_list.entries[i - 1];
            }

            /* Insert kickstart entry at position 0 */
            entry = &libraries_list.entries[0];
            strncpy(entry->name, "kick update", sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            entry->location = LOC_KICKSTART;
            /* ROM base: 0x00f80000 for 512K, 0x00fc0000 for 256K */
            entry->address = (APTR)(hw_info.kickstart_size >= 512 ? 0x00f80000 : 0x00fc0000);
            entry->version = hw_info.kickstart_patch_version;
            entry->revision = hw_info.kickstart_patch_revision;

            libraries_list.count++;

        }


        /* Shift all entries by 1 position */
        for (i = libraries_list.count; i > 0; i--) {
            libraries_list.entries[i] = libraries_list.entries[i - 1];
        }

        /* Insert kickstart entry at position 0 */
        entry = &libraries_list.entries[0];
        strncpy(entry->name, "kickstart", sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->location = LOC_KICKSTART;
        /* ROM base: 0x00f80000 for 512K, 0x00fc0000 for 256K */
        entry->address = (APTR)(hw_info.kickstart_size >= 512 ? 0x00f80000 : 0x00fc0000);
        entry->version = hw_info.kickstart_version;
        entry->revision = hw_info.kickstart_revision;

        libraries_list.count++;
    }
}

/*
 * Enumerate all open devices
 */
void enumerate_devices(void)
{
    struct Device *dev;

    memset(&devices_list, 0, sizeof(devices_list));

    Forbid();

    for (dev = (struct Device *)SysBase->DeviceList.lh_Head;
         (struct Node *)dev != (struct Node *)&SysBase->DeviceList.lh_Tail;
         dev = (struct Device *)dev->dd_Library.lib_Node.ln_Succ) {

        if (devices_list.count >= MAX_SOFTWARE_ENTRIES) break;

        SoftwareEntry *entry = &devices_list.entries[devices_list.count];

        if (dev->dd_Library.lib_Node.ln_Name) {
            copy_base_name(entry->name, dev->dd_Library.lib_Node.ln_Name,
                           sizeof(entry->name));
        } else {
            strncpy(entry->name, "(unknown)", sizeof(entry->name) - 1);
        }

        entry->address = (APTR)dev;
        entry->version = dev->dd_Library.lib_Version;
        entry->revision = dev->dd_Library.lib_Revision;
        entry->location = determine_mem_location((APTR)dev);

        devices_list.count++;
    }

    Permit();

    sort_software_list(&devices_list);
}

/*
 * Enumerate all resources
 */
void enumerate_resources(void)
{
    struct Library *res;

    memset(&resources_list, 0, sizeof(resources_list));

    Forbid();

    for (res = (struct Library *)SysBase->ResourceList.lh_Head;
         (struct Node *)res != (struct Node *)&SysBase->ResourceList.lh_Tail;
         res = (struct Library *)res->lib_Node.ln_Succ) {

        if (resources_list.count >= MAX_SOFTWARE_ENTRIES) break;

        SoftwareEntry *entry = &resources_list.entries[resources_list.count];

        if (res->lib_Node.ln_Name) {
            copy_base_name(entry->name, res->lib_Node.ln_Name, sizeof(entry->name));
        } else {
            strncpy(entry->name, "(unknown)", sizeof(entry->name) - 1);
        }

        entry->address = (APTR)res;
        entry->version = res->lib_Version;
        entry->revision = res->lib_Revision;
        entry->location = determine_mem_location((APTR)res);

        resources_list.count++;
    }

    Permit();

    sort_software_list(&resources_list);
}

void enumerate_mmu_entries(void)
{
    struct MinList *list;
    struct MappingNode *mn;
    SoftwareEntry *entry;
    char buffer[128];
    memset(&mmu_list, 0, sizeof(mmu_list));

    Forbid();

    //is mmu.library loaded?
    if (mmuLoaded && hw_info.mmu_enabled) {
        //no else: iff mmu.library is in the libraries lsit, it can load!
        if ((DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37L))) {
            if ((MMUBase = OpenLibrary((CONST_STRPTR)"mmu.library", 40L))) {

                entry = &mmu_list.entries[mmu_list.count];
                snprintf(entry->name, sizeof(entry->name), "%s: %lukB.", get_string(MSG_MMU_SIZE), (unsigned long)(GetPageSize(NULL)/1024));
                mmu_list.count++;
                entry = &mmu_list.entries[mmu_list.count];
                snprintf(entry->name, sizeof(entry->name), "%s",get_string(MSG_MMU_ADDRESS_HINT));
                mmu_list.count++;
                /* Get the mapping of the default context */
                list=GetMapping(NULL);
                for (mn = (struct MappingNode *)(list->mlh_Head); mn->map_succ; mn = mn->map_succ)
                {
                    size_t pos;
                    if (mmu_list.count >= MAX_SOFTWARE_ENTRIES) break;
                    memset(buffer, 0, sizeof(buffer));
                    pos = snprintf(buffer, sizeof(buffer), "%08lx-%08lx", mn->map_Lower, mn->map_Higher);
                    if (mn->map_Properties & MAPP_WINDOW)
                    {
                        pos += snprintf(buffer + pos, sizeof(buffer) - pos, " Window %08lx", ((ULONG)mn->map_un.map_UserData));
                        /* All other flags do not care then */
                    }
                    else {
                        if (mn->map_Properties & MAPP_WRITEPROTECTED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " WP");
                        }

                        if (mn->map_Properties & MAPP_USED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " U");
                        }

                        if (mn->map_Properties & MAPP_MODIFIED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " M");
                        }

                        if (mn->map_Properties & MAPP_GLOBAL) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " G");
                        }

                        if (mn->map_Properties & MAPP_TRANSLATED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " TT");
                        }

                        if (mn->map_Properties & MAPP_ROM) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " ROM");
                        }

                        if (mn->map_Properties & MAPP_USERPAGE0) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " UP0");
                        }

                        if (mn->map_Properties & MAPP_USERPAGE1) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " UP1");
                        }

                        if (mn->map_Properties & MAPP_CACHEINHIBIT) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " CI");
                        }

                        if (mn->map_Properties & MAPP_IMPRECISE) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " IM");
                        }

                        if (mn->map_Properties & MAPP_NONSERIALIZED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " NS");
                        }

                        if (mn->map_Properties & MAPP_COPYBACK) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " CB");
                        }

                        if (mn->map_Properties & MAPP_SUPERVISORONLY) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " SO");
                        }

                        if (mn->map_Properties & MAPP_BLANK) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " BL");
                        }

                        if (mn->map_Properties & MAPP_SHARED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " SH");
                        }

                        if (mn->map_Properties & MAPP_SINGLEPAGE) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " SNG");
                        }

                        if (mn->map_Properties & MAPP_REPAIRABLE) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " RP");
                        }

                        if (mn->map_Properties & MAPP_IO) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " IO");
                        }

                        if (mn->map_Properties & MAPP_USER0) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " U0");
                        }

                        if (mn->map_Properties & MAPP_USER1) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " U1");
                        }

                        if (mn->map_Properties & MAPP_USER2) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " U2");
                        }

                        if (mn->map_Properties & MAPP_USER3) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " U3");
                        }

                        if (mn->map_Properties & MAPP_INVALID) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " INV %08lx", ((ULONG)mn->map_un.map_UserData));
                        }

                        if (mn->map_Properties & MAPP_SWAPPED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " SW %08lx", ((ULONG)mn->map_un.map_UserData));
                        }

                        if (mn->map_Properties & MAPP_REMAPPED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " MAP %08lx", ((ULONG)(mn->map_un.map_Delta + mn->map_Lower)));
                        }

                        if (mn->map_Properties & MAPP_BUNDLED) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " BN %08lx", ((ULONG)mn->map_un.map_Page));
                        }

                        if (mn->map_Properties & MAPP_INDIRECT) {
                            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " IND %08lx", ((ULONG)mn->map_un.map_Descriptor));
                        }
                    }
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s", buffer);
                    mmu_list.count++;
                }
                CloseLibrary((struct Library *)MMUBase);
            }
            CloseLibrary((struct Library *)DOSBase);
        }
    }
    else {
        SoftwareEntry *entry = &mmu_list.entries[0];
        strncpy(entry->name, "mmu.library not loaded", sizeof(entry->name) - 1);
        mmu_list.count++;
    }
    Permit();
}

/*
 * Enumerate all software types
 */
void enumerate_all_software(void)
{
    enumerate_libraries();
    enumerate_devices();
    enumerate_resources();
    enumerate_mmu_entries();
}

/*
 * Get the appropriate list for a software type
 */
SoftwareList *get_software_list(SoftwareType type)
{
    switch (type) {
        case SOFTWARE_LIBRARIES:
            return &libraries_list;
        case SOFTWARE_DEVICES:
            return &devices_list;
        case SOFTWARE_RESOURCES:
            return &resources_list;
        case SOFTWARE_MMU:
            return &mmu_list;
        default:
            return NULL;
    }
}
