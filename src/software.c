// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - System software enumeration (libraries, devices, resources)
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <libraries/identify.h>

#include <proto/exec.h>
#include <proto/mmu.h>
#include <proto/identify.h>

#include "xsysinfo.h"
#include "software.h"
#include "hardware.h"
#include "locale_str.h"

/* Global software lists */
SoftwareList libraries_list;
SoftwareList devices_list;
SoftwareList resources_list;
SoftwareList mmu_list;
SystemSoftwareInfo system_software;

/* External references */
extern struct ExecBase *SysBase;

/* Global variables*/
BOOL mmuLoaded = FALSE;
extern struct Library *MMUBase;
extern struct DosLibrary *DOSBase;

/*
 * Copy a module name, omitting a recognized file-type suffix.
 * Preserve other dots, for example in names containing version numbers.
 */
static void copy_base_name(char *dest, const char *src, size_t destsize)
{
    static const char *const suffixes[] = {
        ".library", ".device", ".resource", ".datatype",
        ".gadget", ".image", ".class", ".mcc", ".mcp"
    };
    const char *dot;
    size_t len, i;

    if (!dest || destsize == 0) return;
    if (!src) src = "(unknown)";

    len = strlen(src);
    dot = strrchr(src, '.');
    if (dot && dot > src) {
        for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
            if (strcmp(dot, suffixes[i]) == 0) {
                len = (size_t)(dot - src);
                break;
            }
        }
    }
    if (len >= destsize) len = destsize - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static size_t append_format(char *buffer, size_t buffer_size, size_t pos,
                            const char *format, ...)
{
    va_list args;
    int written;

    if (buffer_size == 0) {
        return 0;
    }

    if (pos >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return buffer_size - 1;
    }

    va_start(args, format);
    written = vsnprintf(buffer + pos, buffer_size - pos, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= buffer_size - pos) {
        buffer[buffer_size - 1] = '\0';
        return buffer_size - 1;
    }

    return pos + (size_t)written;
}

/* Comparison function for sorting */
static int compare_entries(const void *a, const void *b)
{
    const SoftwareEntry *ea = (const SoftwareEntry *)a;
    const SoftwareEntry *eb = (const SoftwareEntry *)b;
    return strcmp(ea->name, eb->name);
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
    struct Node *node;
    ULONG i;
    SoftwareEntry *entry;

    memset(&libraries_list, 0, sizeof(libraries_list));

    Forbid();

    for (node = SysBase->LibList.lh_Head; node && node->ln_Succ;
         node = node->ln_Succ) {
        struct Library *lib = (struct Library *)node;

        /* Detect FPU library presence (outside entry limit check) */
        if (lib->lib_Node.ln_Name) {
            if (strcmp(lib->lib_Node.ln_Name, "68040.library") == 0) {
                if (hw_info.fpu_type == FPU_68040)
                    hw_info.fpu_enabled = TRUE;
            }
            if (strcmp(lib->lib_Node.ln_Name, "68060.library") == 0) {
                if (hw_info.fpu_type == FPU_68060)
                    hw_info.fpu_enabled = TRUE;
            }
            if (strcmp(lib->lib_Node.ln_Name, "mmu.library") == 0) {
                mmuLoaded = TRUE;
            }
        }

        if (libraries_list.count >= MAX_SOFTWARE_ENTRIES)
            continue;

        entry = &libraries_list.entries[libraries_list.count];

        copy_base_name(entry->name, lib->lib_Node.ln_Name, sizeof(entry->name));

        entry->address = (APTR)lib;
        entry->version = lib->lib_Version;
        entry->revision = lib->lib_Revision;
        /* Classify by physical location; the base address can be
         * MMU-remapped to other memory (issue #44) */
        entry->location = determine_mem_location(mmu_physical_address((APTR)lib));

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
            hw_info.kickstart_version >= 40 /* softkick from Kick 3.1 (v40)+ */
        ) {
            /* Shift all entries by 1 position */
            for (i = libraries_list.count; i > 0; i--) {
                libraries_list.entries[i] = libraries_list.entries[i - 1];
            }

            /* Insert kickstart entry at position 0 */
            entry = &libraries_list.entries[0];
            copy_string(entry->name, "kick update", sizeof(entry->name));
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
        copy_string(entry->name, "kickstart", sizeof(entry->name));
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
    struct Node *node;

    memset(&devices_list, 0, sizeof(devices_list));

    Forbid();

    for (node = SysBase->DeviceList.lh_Head; node && node->ln_Succ;
         node = node->ln_Succ) {
        struct Device *dev = (struct Device *)node;

        if (devices_list.count >= MAX_SOFTWARE_ENTRIES) break;

        SoftwareEntry *entry = &devices_list.entries[devices_list.count];

        copy_base_name(entry->name, dev->dd_Library.lib_Node.ln_Name,
                       sizeof(entry->name));

        entry->address = (APTR)dev;
        entry->version = dev->dd_Library.lib_Version;
        entry->revision = dev->dd_Library.lib_Revision;
        entry->location = determine_mem_location(mmu_physical_address((APTR)dev));

        devices_list.count++;
    }

    Permit();

    sort_software_list(&devices_list);
}

/*
 * Parse the first "<major>.<minor>" pair out of a Resident's rt_IdString
 * (the "$VER: name major.minor (date)" string). Digit runs that are not
 * followed by ".<digit>" (e.g. numbers embedded in the module name like
 * "a2091.device") are skipped, so the first real version.revision pair wins.
 */
static BOOL parse_id_version(const char *s, UWORD *ver, UWORD *rev)
{
    if (!s) return FALSE;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            const char *p = s;
            UWORD major = 0, minor = 0;

            while (*p >= '0' && *p <= '9')
                major = (UWORD)(major * 10 + (*p++ - '0'));

            if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
                p++;
                while (*p >= '0' && *p <= '9')
                    minor = (UWORD)(minor * 10 + (*p++ - '0'));
                *ver = major;
                *rev = minor;
                return TRUE;
            }
            s = p;
        } else {
            s++;
        }
    }

    return FALSE;
}

/*
 * Enumerate all resources
 */
void enumerate_resources(void)
{
    struct Node *node;

    memset(&resources_list, 0, sizeof(resources_list));

    Forbid();

    for (node = SysBase->ResourceList.lh_Head; node && node->ln_Succ;
         node = node->ln_Succ) {
        struct Library *res = (struct Library *)node;

        if (resources_list.count >= MAX_SOFTWARE_ENTRIES) break;

        SoftwareEntry *entry = &resources_list.entries[resources_list.count];

        copy_base_name(entry->name, res->lib_Node.ln_Name, sizeof(entry->name));

        entry->address = (APTR)res;

        /*
         * Resource nodes are not struct Library: only the leading struct Node
         * is guaranteed, so lib_Version/lib_Revision read whatever resource-
         * specific data happens to sit at those offsets (see issue #51, where
         * filesystem.resource showed "65108.0"). Take the version from the
         * matching Resident module instead, mirroring what "version <name>"
         * reports. Resources with no resident (e.g. the runtime-created
         * ciaa/ciab.resource) are left at 0.0.
         */
        entry->version = 0;
        entry->revision = 0;
        if (res->lib_Node.ln_Name) {
            struct Resident *rt =
                FindResident((CONST_STRPTR)res->lib_Node.ln_Name);
            if (rt) {
                UWORD v = 0, r = 0;
                if (parse_id_version(rt->rt_IdString, &v, &r)) {
                    entry->version = v;
                    entry->revision = r;
                } else {
                    entry->version = rt->rt_Version;
                }
            }
        }

        entry->location = determine_mem_location(mmu_physical_address((APTR)res));

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
        //no else: iff mmu.library is in the libraries list, it can load!
        if (DOSBase && DOSBase->dl_lib.lib_Version >= 37) {
            if ((MMUBase = open_mmu_library())) {

                entry = &mmu_list.entries[mmu_list.count];
                snprintf(entry->name, sizeof(entry->name), "%s: %lukB.",
                         get_string(MSG_MMU_SIZE),
                         (unsigned long)(GetPageSize(NULL) / 1024));
                mmu_list.count++;
                /* Get the mapping of the default context */
                list = GetMapping(NULL);
                for (mn = list ? (struct MappingNode *)(list->mlh_Head) : NULL;
                     mn && mn->map_succ && mmu_list.count < 256;
                     mn = mn->map_succ)
                {
                    size_t pos;
                    memset(buffer, 0, sizeof(buffer));
                    pos = append_format(buffer, sizeof(buffer), 0,
                                        "%08lx-%08lx",
                                        (unsigned long)mn->map_Lower,
                                        (unsigned long)mn->map_Higher);
                    if (mn->map_Properties & MAPP_WINDOW)
                    {
                        pos = append_format(buffer, sizeof(buffer), pos,
                                            " Window %08lx",
                                            (unsigned long)mn->map_un.map_UserData);
                        /* All other flags do not care then */
                    }
                    else {
                        if (mn->map_Properties & MAPP_WRITEPROTECTED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " WP");
                        }

                        if (mn->map_Properties & MAPP_USED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " U");
                        }

                        if (mn->map_Properties & MAPP_MODIFIED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " M");
                        }

                        if (mn->map_Properties & MAPP_GLOBAL) {
                            pos = append_format(buffer, sizeof(buffer), pos, " G");
                        }

                        if (mn->map_Properties & MAPP_TRANSLATED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " TT");
                        }

                        if (mn->map_Properties & MAPP_ROM) {
                            pos = append_format(buffer, sizeof(buffer), pos, " ROM");
                        }

                        if (mn->map_Properties & MAPP_USERPAGE0) {
                            pos = append_format(buffer, sizeof(buffer), pos, " UP0");
                        }

                        if (mn->map_Properties & MAPP_USERPAGE1) {
                            pos = append_format(buffer, sizeof(buffer), pos, " UP1");
                        }

                        if (mn->map_Properties & MAPP_CACHEINHIBIT) {
                            pos = append_format(buffer, sizeof(buffer), pos, " CI");
                        }

                        if (mn->map_Properties & MAPP_IMPRECISE) {
                            pos = append_format(buffer, sizeof(buffer), pos, " IM");
                        }

                        if (mn->map_Properties & MAPP_NONSERIALIZED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " NS");
                        }

                        if (mn->map_Properties & MAPP_COPYBACK) {
                            pos = append_format(buffer, sizeof(buffer), pos, " CB");
                        }

                        if (mn->map_Properties & MAPP_SUPERVISORONLY) {
                            pos = append_format(buffer, sizeof(buffer), pos, " SO");
                        }

                        if (mn->map_Properties & MAPP_BLANK) {
                            pos = append_format(buffer, sizeof(buffer), pos, " BL");
                        }

                        if (mn->map_Properties & MAPP_SHARED) {
                            pos = append_format(buffer, sizeof(buffer), pos, " SH");
                        }

                        if (mn->map_Properties & MAPP_SINGLEPAGE) {
                            pos = append_format(buffer, sizeof(buffer), pos, " SNG");
                        }

                        if (mn->map_Properties & MAPP_REPAIRABLE) {
                            pos = append_format(buffer, sizeof(buffer), pos, " RP");
                        }

                        if (mn->map_Properties & MAPP_IO) {
                            pos = append_format(buffer, sizeof(buffer), pos, " IO");
                        }

                        if (mn->map_Properties & MAPP_USER0) {
                            pos = append_format(buffer, sizeof(buffer), pos, " U0");
                        }

                        if (mn->map_Properties & MAPP_USER1) {
                            pos = append_format(buffer, sizeof(buffer), pos, " U1");
                        }

                        if (mn->map_Properties & MAPP_USER2) {
                            pos = append_format(buffer, sizeof(buffer), pos, " U2");
                        }

                        if (mn->map_Properties & MAPP_USER3) {
                            pos = append_format(buffer, sizeof(buffer), pos, " U3");
                        }

                        if (mn->map_Properties & MAPP_INVALID) {
                            pos = append_format(buffer, sizeof(buffer), pos,
                                                " INV %08lx",
                                                (unsigned long)mn->map_un.map_UserData);
                        }

                        if (mn->map_Properties & MAPP_SWAPPED) {
                            pos = append_format(buffer, sizeof(buffer), pos,
                                                " SW %08lx",
                                                (unsigned long)mn->map_un.map_UserData);
                        }

                        if (mn->map_Properties & MAPP_REMAPPED) {
                            pos = append_format(buffer, sizeof(buffer), pos,
                                                " MAP %08lx",
                                                (unsigned long)(mn->map_un.map_Delta +
                                                                mn->map_Lower));
                        }

                        if (mn->map_Properties & MAPP_BUNDLED) {
                            pos = append_format(buffer, sizeof(buffer), pos,
                                                " BN %08lx",
                                                (unsigned long)mn->map_un.map_Page);
                        }

                        if (mn->map_Properties & MAPP_INDIRECT) {
                            pos = append_format(buffer, sizeof(buffer), pos,
                                                " IND %08lx",
                                                (unsigned long)mn->map_un.map_Descriptor);
                        }
                    }
                    entry = &mmu_list.entries[mmu_list.count];
                    copy_string(entry->name, buffer, sizeof(entry->name));
                    mmu_list.count++;
                }
                if (list) {
                    ReleaseMapping(NULL, list);
                }
                /* Append hint entries at end of list */
                if (mmu_list.count < 256 - 8) {
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_ADDRESS_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS1_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS2_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS3_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS4_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS5_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS6_HINT));
                    mmu_list.count++;
                    entry = &mmu_list.entries[mmu_list.count];
                    snprintf(entry->name, sizeof(entry->name), "%s",
                             get_string(MSG_MMU_FLAGS7_HINT));
                    mmu_list.count++;
                }

                CloseLibrary((struct Library *)MMUBase);
            }
        }
    } else {
        entry = &mmu_list.entries[0];
        copy_string(entry->name, "mmu.library not loaded",
                    sizeof(entry->name));
        mmu_list.count++;
    }
    Permit();
}

static void detect_graphics_system(void)
{
    const char *name;

    system_software.graphics_system_id = IdentifyBase ?
        IdHardwareNumTags(IDHW_GFXSYS, TAG_DONE) : IDGOS_AMIGAOS;
    switch (system_software.graphics_system_id) {
    case IDGOS_EGS:       name = "EGS"; break;
    case IDGOS_RETINA:    name = "Retina"; break;
    case IDGOS_GRAFFITI:  name = "Graffiti"; break;
    case IDGOS_TIGA:      name = "TIGA"; break;
    case IDGOS_PROBENCH:  name = "ProBench"; break;
    case IDGOS_PICASSO:
    case IDGOS_PICASSO96: name = "Picasso96"; break;
    case IDGOS_CGX:       name = "CyberGraphX V2"; break;
    case IDGOS_CGX3:      name = "CyberGraphX V3"; break;
    case IDGOS_CGX4:      name = "CyberGraphX V4"; break;
    default:
        system_software.graphics_system_id = IDGOS_AMIGAOS;
        name = get_string(MSG_NATIVE_GRAPHICS);
        break;
    }
    copy_string(system_software.graphics_system, name,
                sizeof(system_software.graphics_system));
}

void detect_system_software(void)
{
    /* TinySetPatch appends this record at byte 80 of its SetPatch semaphore. */
    struct TinySetPatchInfo {
        ULONG magic[2];
        UWORD version;
        UWORD revision;
    } const volatile *tinysetpatch;
    struct SignalSemaphore *semaphore;
    struct Library *version_base;
    ULONG setpatch = 0;
    STRPTR os_name;

    memset(&system_software, 0, sizeof(system_software));
    version_base = OpenLibrary((CONST_STRPTR)"version.library", 0);
    if (version_base) {
        system_software.has_workbench_version = TRUE;
        system_software.workbench_version = version_base->lib_Version;
        system_software.workbench_revision = version_base->lib_Revision;
        CloseLibrary(version_base);
    }

    if (IdentifyBase) {
        system_software.os_id = IdHardwareNumTags(IDHW_OSNR, TAG_DONE);
        os_name = IdHardwareTags(IDHW_OSNR, TAG_DONE);
        /* Unknown IDs are described as "Bad OS" by identify.library. */
        if (system_software.os_id != IDOS_UNKNOWN && os_name)
            copy_string(system_software.os_name, (const char *)os_name,
                        sizeof(system_software.os_name));
        setpatch = IdHardwareNumTags(IDHW_SETPATCHVER, TAG_DONE);
    }
    /* Older identify.library versions do not recognize the V48 ROMs. */
    if (!system_software.os_name[0] && SysBase->LibNode.lib_Version == 48)
        copy_string(system_software.os_name, "AmigaOS 3.3",
                    sizeof(system_software.os_name));
    if (setpatch) {
        system_software.has_setpatch_version = TRUE;
        system_software.setpatch_version = setpatch & 0xffff;
        system_software.setpatch_revision = setpatch >> 16;
        Forbid();
        semaphore = FindSemaphore((CONST_STRPTR)"\253 SetPatch \273");
        if (semaphore) {
            /* Older implementations have no extension; require both magic
             * words before interpreting the following version fields. */
            tinysetpatch = (const volatile struct TinySetPatchInfo *)
                ((const UBYTE *)semaphore + 80);
            if (tinysetpatch->magic[0] == 0x54696e79UL &&
                tinysetpatch->magic[1] == 0x53657450UL) {
                system_software.is_tinysetpatch = TRUE;
                system_software.tinysetpatch_version = tinysetpatch->version;
                system_software.tinysetpatch_revision = tinysetpatch->revision;
            }
        }
        Permit();
    }
    detect_graphics_system();
}

/*
 * Enumerate all software types
 */
void enumerate_all_software(void)
{
    detect_system_software();
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
