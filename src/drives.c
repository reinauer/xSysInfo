// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Drives enumeration and view
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <devices/timer.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>

/* 64-bit trackdisk read via NSD; not in all NDK headers */
#ifndef NSCMD_TD_READ64
#define NSCMD_TD_READ64 0xC000
#endif
#ifndef TD_READ64
#define TD_READ64 (CMD_NONSTD + 15)
#endif

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>

#include "xsysinfo.h"
#include "drives.h"
#include "scsi.h"
#include "gui.h"
#include "benchmark.h"
#include "locale_str.h"
#include "debug.h"
#include "hardware.h"
#include <limits.h>

/* Global drive list */
DriveList drive_list;

/* Drive list pagination: the view shows one page of buttons at a time */
#define DRIVES_PER_PAGE 10

#define DRIVE_SPEED_FIRST_CHUNK (64UL * 1024UL)
#define DRIVE_SPEED_HARD_CHUNK  (256UL * 1024UL)
#define DRIVE_SPEED_TARGET_US   1000000UL
#define DRIVE_SPEED_MAX_US      2000000UL
#define DRIVE_SPEED_TOTAL_US    (DRIVE_SPEED_MAX_US * DRIVE_SPEED_SAMPLES)
#define DRIVE_SPEED_MAX_BYTES   (32UL * 1024UL * 1024UL)
#define DRIVE_SPEED_FLOPPY_TRACKS 3UL
#define DRIVE_SPEED_DD_SECTORS    11UL

#define DRIVE_INFO_X        120
#define DRIVE_VALUE_OFFSET  224
#define DRIVE_VALUE_X       (DRIVE_INFO_X + DRIVE_VALUE_OFFSET)
#define DRIVE_VALUE_MAX_X   618

static ULONG drive_page = 0;

/* External references */
extern AppContext *app;
extern Button buttons[];
extern int num_buttons;
extern struct DosLibrary *DOSBase;

/* DOS type identifiers */
/* ID_DOS_DISK and ID_FFS_DISK are defined in dos/dos.h */
     // ID_DOS_DISK     0x444F5300  /* 'DOS\0' */
     // ID_FFS_DISK     0x444F5301  /* 'DOS\1' */
#define ID_INTER_DOS    0x444F5302  /* 'DOS\2' */
#define ID_INTER_FFS    0x444F5303  /* 'DOS\3' */
#define ID_DC_DOS       0x444F5304  /* 'DOS\4' */
#define ID_DC_FFS       0x444F5305  /* 'DOS\5' */
#define ID_LNFS_DOS     0x444F5306  /* 'DOS\6' */
#define ID_LNFS_FFS     0x444F5307  /* 'DOS\7' */
#define ID_SFS_BE       0x53465300  /* 'SFS\0' */
#define ID_PFS0         0x50465300  /* 'PFS\0' */
#define ID_PFS1         0x50465301  /* 'PFS\1' */
#define ID_PFS2         0x50465302  /* 'PFS\2' */
#define ID_PFS3         0x50465303  /* 'PFS\3' */
#define ID_PDS0         0x50445300  /* 'PDS\0' */
#define ID_PDS1         0x50445301  /* 'PDS\1' */
#define ID_PDS2         0x50445302  /* 'PDS\2' */
#define ID_PDS3         0x50445303  /* 'PDS\3' */

#define OFS_BLOCK_OVERHEAD 24UL

/*
 * Identify filesystem from DOS type
 */
FilesystemType identify_filesystem(ULONG dos_type)
{
    switch (dos_type) {
        case ID_DOS_DISK:
            return FS_OFS;
        case ID_FFS_DISK:
            return FS_FFS;
        case ID_INTER_DOS:
            return FS_INTL_OFS;
        case ID_INTER_FFS:
            return FS_INTL_FFS;
        case ID_DC_DOS:
            return FS_DCACHE_OFS;
        case ID_DC_FFS:
            return FS_DCACHE_FFS;
        case ID_LNFS_DOS:
            return FS_LNFS_OFS;
        case ID_LNFS_FFS:
            return FS_LNFS_FFS;
        case ID_SFS_BE:
            return FS_SFS;
        case ID_PFS0:
        case ID_PFS1:
        case ID_PFS2:
        case ID_PFS3:
        case ID_PDS0:
        case ID_PDS1:
        case ID_PDS2:
        case ID_PDS3:
            return FS_PFS;
        default:
            return FS_UNKNOWN;
    }
}

/*
 * Get filesystem type string
 */
const char *get_filesystem_string(FilesystemType fs)
{
    switch (fs) {
        case FS_OFS:        return get_string(MSG_OFS);
        case FS_FFS:        return get_string(MSG_FFS);
        case FS_INTL_OFS:   return get_string(MSG_INTL_OFS);
        case FS_INTL_FFS:   return get_string(MSG_INTL_FFS);
        case FS_DCACHE_OFS: return get_string(MSG_DCACHE_OFS);
        case FS_DCACHE_FFS: return get_string(MSG_DCACHE_FFS);
        case FS_LNFS_OFS:   return get_string(MSG_LNFS_OFS);
        case FS_LNFS_FFS:   return get_string(MSG_LNFS_FFS);
        case FS_SFS:        return get_string(MSG_SFS);
        case FS_PFS:        return get_string(MSG_PFS);
        default:            return get_string(MSG_UNKNOWN_FS);
    }
}

static BOOL is_ofs_filesystem(FilesystemType fs)
{
    switch (fs) {
        case FS_OFS:
        case FS_INTL_OFS:
        case FS_DCACHE_OFS:
        case FS_LNFS_OFS:
            return TRUE;
        default:
            return FALSE;
    }
}

static char dos_type_id_char(ULONG dos_type, ULONG shift)
{
    UBYTE ch = (UBYTE)(dos_type >> shift);

    if (ch >= 32 && ch <= 126 && ch != '\\') {
        return (char)ch;
    }
    return '?';
}

static void format_dos_type_string(ULONG dos_type, char *buffer, ULONG size)
{
    if (!buffer || size == 0) return;

    snprintf(buffer, size, "%c%c%c\\%lu",
             dos_type_id_char(dos_type, 24),
             dos_type_id_char(dos_type, 16),
             dos_type_id_char(dos_type, 8),
             (unsigned long)(dos_type & 0xff));
}

void format_filesystem_display(const DriveInfo *drive, char *buffer,
                               ULONG size)
{
    const char *name;
    char dos_type[16];

    if (!buffer || size == 0) return;
    buffer[0] = '\0';
    if (!drive) return;

    name = drive->fs_type == FS_UNKNOWN ?
           "Unknown" : get_filesystem_string(drive->fs_type);

    if (drive->has_dos_type) {
        format_dos_type_string(drive->dos_type, dos_type, sizeof(dos_type));
        snprintf(buffer, size, "%s (%s)", name, dos_type);
    } else {
        snprintf(buffer, size, "%s", name);
    }
}

/*
 * Get disk state string
 */
const char *get_disk_state_string(DiskState state)
{
    switch (state) {
        case DISK_OK:               return get_string(MSG_DISK_OK);
        case DISK_WRITE_PROTECTED:  return get_string(MSG_DISK_WRITE_PROTECTED);
        case DISK_NO_DISK:          return get_string(MSG_DISK_NO_DISK);
        default:                    return get_string(MSG_UNKNOWN);
    }
}

ULONG get_display_block_size(const DriveInfo *drive)
{
    if (!drive) return 0;

    if (drive->bytes_per_block) {
        return drive->bytes_per_block;
    }

    if (drive->has_info && drive->info_bytes_per_block) {
        if (is_ofs_filesystem(drive->fs_type) &&
            drive->info_bytes_per_block > OFS_BLOCK_OVERHEAD) {
            return drive->info_bytes_per_block + OFS_BLOCK_OVERHEAD;
        }
        return drive->info_bytes_per_block;
    }

    return 0;
}

ULONG get_filesystem_block_size(const DriveInfo *drive)
{
    ULONG block_size;

    if (!drive) return 0;

    block_size = drive->filesystem_bytes_per_block;
    if (block_size == 0) {
        block_size = get_display_block_size(drive);
    }

    return block_size;
}

void format_block_size_display(const DriveInfo *drive, char *buffer,
                               ULONG size)
{
    ULONG block_size;
    ULONG filesystem_block_size;
    ULONG payload_block_size = 0;

    if (!buffer || size == 0) return;

    block_size = get_display_block_size(drive);
    filesystem_block_size = get_filesystem_block_size(drive);
    if (drive && is_ofs_filesystem(drive->fs_type) &&
        filesystem_block_size > OFS_BLOCK_OVERHEAD) {
        payload_block_size = filesystem_block_size - OFS_BLOCK_OVERHEAD;
    }

    if (payload_block_size != 0 &&
        filesystem_block_size != 0 && filesystem_block_size != block_size) {
        snprintf(buffer, size,
                 "%lu bytes (filesystem %lu bytes, payload %lu bytes)",
                 (unsigned long)block_size,
                 (unsigned long)filesystem_block_size,
                 (unsigned long)payload_block_size);
    } else if (payload_block_size != 0) {
        snprintf(buffer, size, "%lu bytes (payload %lu bytes)",
                 (unsigned long)block_size,
                 (unsigned long)payload_block_size);
    } else if (filesystem_block_size != 0 &&
               filesystem_block_size != block_size) {
        snprintf(buffer, size, "%lu bytes (filesystem %lu bytes)",
                 (unsigned long)block_size,
                 (unsigned long)filesystem_block_size);
    } else {
        snprintf(buffer, size, "%lu bytes", (unsigned long)block_size);
    }
}

static ULONG get_geometry_total_blocks(const DriveInfo *drive)
{
    if (!drive) return 0;
    if (drive->geometry_total_blocks) return drive->geometry_total_blocks;
    return drive->total_blocks;
}

static BPTR lock_drive_for_info(const DriveInfo *drive, BOOL silent)
{
    struct Process *proc = NULL;
    APTR old_window = NULL;
    BPTR lock;

    if (!drive) return 0;

    if (silent) {
        proc = (struct Process *)FindTask(NULL);
        if (proc) {
            old_window = proc->pr_WindowPtr;
            proc->pr_WindowPtr = (APTR)-1;
        }
    }

    lock = Lock((CONST_STRPTR)drive->device_name, ACCESS_READ);

    if (silent && proc) {
        proc->pr_WindowPtr = old_window;
    }

    return lock;
}

/*
 * Convert BSTR to C string
 */
static void bstr_to_cstr(BSTR bstr, char *cstr, ULONG maxlen)
{
    UBYTE *src = BADDR(bstr);
    UBYTE len;

    if (!src || !cstr) return;

    len = *src++;
    if (len >= maxlen) len = maxlen - 1;

    memcpy(cstr, src, len);
    cstr[len] = '\0';
}

static void bstr_to_device_name(BSTR bstr, char *name, ULONG maxlen)
{
    size_t len;

    if (!name || maxlen == 0) return;
    if (maxlen == 1) {
        name[0] = '\0';
        return;
    }

    name[0] = '\0';
    bstr_to_cstr(bstr, name, maxlen - 1);
    len = strlen(name);
    name[len++] = ':';
    name[len] = '\0';
}

/* Helpers */
static BOOL is_floppy_device(ULONG total_blocks);
static BOOL drive_has_media_evidence(const DriveInfo *drive);

#define MIN_KICK_DEVICE_VERSION 36

/*
 * Kick 2.0/1.3 switch function for LockDosList
 */
static struct DosList *MyLockDosList(ULONG flags)
{
    struct DosList *dol;
    if (hw_info.kickstart_patch_version >= MIN_KICK_DEVICE_VERSION) {
        dol = (struct DosList *)LockDosList(flags);
    } else {
        static struct DosList head;
        struct DosInfo *info;

        Forbid();
        info = (struct DosInfo *)BADDR(DOSBase->dl_Root->rn_Info);
        /* NextDosEntry expects a cursor before the first real entry. */
        head.dol_Next = info->di_DevInfo;
        dol = &head;
    }
    return dol;
}

/*
 * Kick 2.0/1.3 switch function for UnLockDosList
 */
static void MyUnLockDosList(ULONG flags)
{
    if (hw_info.kickstart_patch_version >= MIN_KICK_DEVICE_VERSION) {
        UnLockDosList(flags);
    } else {
        Permit();
    }
}

/*
 * Kick 2.0/1.3 switch function for NextDosEntry
 */
static struct DosList *MyNextDosEntry(struct DosList *list, ULONG flags)
{
    struct DosList *dol;
    if (hw_info.kickstart_patch_version >= MIN_KICK_DEVICE_VERSION) {
        dol = (struct DosList *)NextDosEntry(list, flags);
    } else {
        /* Convert LockDosList flags to DosList type flags */
        if (flags == LDF_DEVICES) {
            flags = DLT_DEVICE;
        } else if (flags == LDF_VOLUMES) {
            flags = DLT_VOLUME;
        }
        dol = (struct DosList *)BADDR(list->dol_Next);
        while (dol) {
            if ((ULONG)dol->dol_Type == flags) {
                break;
            }
            dol = (struct DosList *)BADDR(dol->dol_Next);
        }
    }

    return dol;
}

/*
 * Helper: Scan DosList for devices and populate drive list
 */
static void scan_dos_list(void)
{
    struct DosList *dol;

    debug("  drives: Locking DosList...\n");
    dol = MyLockDosList(LDF_DEVICES | LDF_READ);
    debug("  drives: DosList locked\n");

    while ((dol = MyNextDosEntry(dol, LDF_DEVICES)) != NULL) {
        if (drive_list.count >= MAX_DRIVES) {
            debug("  drives: MAX_DRIVES reached, skipping remaining devices\n");
            break;
        }
        DriveInfo *drive = &drive_list.drives[drive_list.count];
        drive->is_valid = FALSE;

        /* Get device name */
        bstr_to_device_name(dol->dol_Name, drive->device_name,
                            sizeof(drive->device_name));

        debug("  drives: Found device '%s' task=$%08lx startup=$%08lx\n",
              (LONG)drive->device_name, (ULONG)dol->dol_Task,
              (ULONG)dol->dol_misc.dol_handler.dol_Startup);

        /* Try to get startup info */
        if (dol->dol_misc.dol_handler.dol_Startup) {
            struct FileSysStartupMsg *fssm;
            fssm = BADDR(dol->dol_misc.dol_handler.dol_Startup);

            /* Check if it looks like a valid FSSM */
            if (fssm && (ULONG)fssm > 0x100) {
                struct DosEnvec *de;

                /* Get handler name */
                bstr_to_cstr(fssm->fssm_Device, drive->handler_name,
                             sizeof(drive->handler_name));
                drive->unit_number = fssm->fssm_Unit;

                de = BADDR(fssm->fssm_Environ);
                if (de && (ULONG)de > 0x100 && de->de_TableSize >= 11) {
                    drive->surfaces = de->de_Surfaces;
                    drive->sectors_per_track = de->de_BlocksPerTrack;
                    drive->reserved_blocks = de->de_Reserved;
                    drive->low_cylinder = de->de_LowCyl;
                    drive->high_cylinder = de->de_HighCyl;
                    drive->bytes_per_block = de->de_SizeBlock << 2;
                    if (drive->bytes_per_block != 0) {
                        ULONG sectors_per_block = de->de_SectorPerBlock;

                        if (sectors_per_block == 0) {
                            sectors_per_block = 1;
                        }
                        if (sectors_per_block <=
                            ULONG_MAX / drive->bytes_per_block) {
                            drive->filesystem_bytes_per_block =
                                drive->bytes_per_block * sectors_per_block;
                        }
                    }
                    drive->num_buffers = de->de_NumBuffers;

                    if (de->de_TableSize >= 12) {
                        drive->buf_mem_type = de->de_BufMemType;
                        drive->has_buf_mem_type = TRUE;
                    }
                    if (de->de_TableSize >= 13) {
                        drive->max_transfer = de->de_MaxTransfer;
                        drive->has_max_transfer = TRUE;
                    }
                    if (de->de_TableSize >= 14) {
                        drive->address_mask = de->de_Mask;
                        drive->has_address_mask = TRUE;
                    }

                    /* Get DOS type if available (de_DosType is at index 16) */
                    if (de->de_TableSize >= 16) {
                        drive->dos_type = de->de_DosType;
                        drive->fs_type = identify_filesystem(de->de_DosType);
                        drive->has_dos_type = TRUE;
                    }

                    /* Calculate geometry blocks */
                    drive->geometry_total_blocks =
                        (drive->high_cylinder - drive->low_cylinder + 1) *
                        drive->surfaces * drive->sectors_per_track;
                    drive->total_blocks = drive->geometry_total_blocks;

                    debug("  drives: %s DosEnvec type=$%08lX block=%lu total=%lu\n",
                          (LONG)drive->device_name,
                          (LONG)drive->dos_type,
                          (LONG)drive->bytes_per_block,
                          (LONG)drive->geometry_total_blocks);
                    debug("  drives: %s DMA hints bufmem=$%08lx max=$%08lx mask=$%08lx\n",
                          (LONG)drive->device_name,
                          (LONG)(drive->has_buf_mem_type ? drive->buf_mem_type : 0),
                          (LONG)(drive->has_max_transfer ? drive->max_transfer : 0),
                          (LONG)(drive->has_address_mask ? drive->address_mask : 0));

                    drive->is_valid = TRUE;
                } else {
                    debug("  drives: %s envec=$%08lx tablesize=%lu not usable\n",
                          (LONG)drive->device_name, (ULONG)de,
                          (ULONG)(de && (ULONG)de > 0x100 ? de->de_TableSize : 0));
                }
            } else {
                debug("  drives: %s startup is not a FSSM\n",
                      (LONG)drive->device_name);
            }
        }

        /* Mark as no disk if we couldn't get FSSM info */
        if (!drive->is_valid) {
            drive->disk_state = DISK_NO_DISK;
        }

        /* Keep every DOS device for now; startup parsing is only
         * enrichment. The final keep/drop decision happens after volume
         * matching and Info(), so drives whose startup is not a plain
         * FileSysStartupMsg (e.g. PiStorm mounters) still show up. */
        drive_list.count++;
    }

    debug("  drives: Unlocking DosList...\n");
    MyUnLockDosList(LDF_DEVICES | LDF_READ);
}

/*
 * Helper: Match volumes to devices
 */
static void match_volumes_to_drives(void)
{
    struct MsgPort *dev_tasks[MAX_DRIVES];
    struct DosList *dev_dol;
    struct DosList *dol;
    ULONG i;

    /* First, collect task pointers for each device */
    memset(dev_tasks, 0, sizeof(dev_tasks));
    dev_dol = MyLockDosList(LDF_DEVICES | LDF_READ);
    while ((dev_dol = MyNextDosEntry(dev_dol, LDF_DEVICES)) != NULL) {
        char dev_name[34];
        bstr_to_device_name(dev_dol->dol_Name, dev_name,
                            sizeof(dev_name));

        /* Find matching drive in our list */
        for (i = 0; i < drive_list.count; i++) {
            if (strcmp(drive_list.drives[i].device_name, dev_name) == 0) {
                dev_tasks[i] = dev_dol->dol_Task;
                break;
            }
        }
    }
    MyUnLockDosList(LDF_DEVICES | LDF_READ);

    /* Now scan volumes and match by task pointer */
    debug("  drives: Looking up volume names...\n");
    dol = MyLockDosList(LDF_VOLUMES | LDF_READ);
    while ((dol = MyNextDosEntry(dol, LDF_VOLUMES)) != NULL) {
        struct MsgPort *vol_task = dol->dol_Task;

        if (!vol_task) continue;

        /* Find device with matching task */
        for (i = 0; i < drive_list.count; i++) {
            if (dev_tasks[i] == vol_task && !drive_list.drives[i].volume_name[0]) {
                bstr_to_cstr(dol->dol_Name, drive_list.drives[i].volume_name,
                             sizeof(drive_list.drives[i].volume_name));
                drive_list.drives[i].disk_state = DISK_OK;
                debug("  drives: Matched volume '%s' to device '%s'\n",
                      (LONG)drive_list.drives[i].volume_name,
                      (LONG)drive_list.drives[i].device_name);
                break;
            }
        }
    }
    MyUnLockDosList(LDF_VOLUMES | LDF_READ);
}

/*
 * Helper: Query detailed drive info using Info()
 */
static void query_drive_details(void)
{
    struct InfoData *info;
    ULONG i;

    info = AllocMem(sizeof(struct InfoData), MEMF_PUBLIC | MEMF_CLEAR);
    if (!info) {
        Printf((CONST_STRPTR)"Out of memory\n");
        return;
    }

    for (i = 0; i < drive_list.count; i++) {
        DriveInfo *drive = &drive_list.drives[i];
        BOOL is_floppy = is_floppy_device(get_geometry_total_blocks(drive));
        BOOL has_volume = drive->volume_name[0] != '\0';
        BPTR lock;

        /* Avoid hanging on empty floppies; require a volume to be present */
        if (!has_volume && is_floppy) {
            drive->disk_state = DISK_NO_DISK;
            continue;
        }
        /* Skip clearly invalid entries */
        if (!drive->is_valid && !has_volume) {
            drive->disk_state = DISK_NO_DISK;
            continue;
        }

        debug("  drives: Trying Info() on '%s'%s\n",
              (LONG)drive->device_name,
              (LONG)(has_volume ? "" : " with requesters suppressed"));
        lock = lock_drive_for_info(drive, !has_volume);
        if (!lock) {
            debug("  drives: Lock failed on '%s'\n", (LONG)drive->device_name);
            if (!has_volume || drive->disk_state == DISK_OK) {
                drive->disk_state = DISK_NO_DISK;
            }
            continue;
        }

        if (Info(lock, info)) {
            FilesystemType info_fs_type;

            drive->info_total_blocks = info->id_NumBlocks;
            drive->info_blocks_used = info->id_NumBlocksUsed;
            drive->info_bytes_per_block = info->id_BytesPerBlock;
            drive->info_disk_type = info->id_DiskType;
            drive->has_info = TRUE;
            info_fs_type = identify_filesystem(drive->info_disk_type);

            drive->total_blocks = drive->info_total_blocks;
            drive->blocks_used = drive->info_blocks_used;
            /* Preserve DosEnvec geometry size for raw device I/O if present. */
            if (drive->bytes_per_block == 0) {
                if (is_ofs_filesystem(info_fs_type) &&
                    drive->info_bytes_per_block > OFS_BLOCK_OVERHEAD) {
                    drive->bytes_per_block =
                        drive->info_bytes_per_block + OFS_BLOCK_OVERHEAD;
                } else {
                    drive->bytes_per_block = drive->info_bytes_per_block;
                }
            }
            if (drive->filesystem_bytes_per_block == 0) {
                if (is_ofs_filesystem(info_fs_type) &&
                    drive->info_bytes_per_block > OFS_BLOCK_OVERHEAD) {
                    drive->filesystem_bytes_per_block =
                        drive->info_bytes_per_block + OFS_BLOCK_OVERHEAD;
                } else {
                    drive->filesystem_bytes_per_block =
                        drive->info_bytes_per_block;
                }
            }
            if (!drive->has_dos_type) {
                drive->dos_type = drive->info_disk_type;
                drive->fs_type = info_fs_type;
                drive->has_dos_type = TRUE;
            }
            drive->disk_errors = info->id_NumSoftErrors;

            debug("  drives: %s Info type=$%08lX block=%lu total=%lu used=%lu\n",
                  (LONG)drive->device_name,
                  (LONG)drive->info_disk_type,
                  (LONG)drive->info_bytes_per_block,
                  (LONG)drive->info_total_blocks,
                  (LONG)drive->info_blocks_used);

            /* Disk state */
            switch (info->id_DiskState) {
                case ID_WRITE_PROTECTED:
                    drive->disk_state = DISK_WRITE_PROTECTED;
                    break;
                case ID_VALIDATED:
                case ID_VALIDATING:
                    drive->disk_state = DISK_OK;
                    break;
                default:
                    drive->disk_state = DISK_UNKNOWN;
                    break;
            }

            /* Volume name (fallback if we missed it earlier) */
            if (info->id_VolumeNode && !drive->volume_name[0]) {
                struct DosList *vol = BADDR(info->id_VolumeNode);
                if (vol) {
                    bstr_to_cstr(vol->dol_Name, drive->volume_name,
                                 sizeof(drive->volume_name));
                }
            }

            drive->is_valid = TRUE;
        } else {
            debug("  drives: Info() failed on '%s'\n", (LONG)drive->device_name);
        }

        UnLock(lock);
    }

    FreeMem(info, sizeof(struct InfoData));
}

/*
 * Helper: Check SCSI direct support for all drives
 *
 * Probes each unique (handler, unit) tuple at most once.  Multiple DH:
 * partitions on the same physical unit would otherwise cause repeated
 * probes against the same device, which has been observed to stall
 * shared SCSI controllers (see issue #13).
 */
static void check_scsi_support_all(void)
{
    ULONG i, j;

    for (i = 0; i < drive_list.count; i++) {
        DriveInfo *drive = &drive_list.drives[i];
        BOOL cached = FALSE;

        if (!drive->handler_name[0]) continue;

        /* Reuse an earlier probe for the same handler+unit. */
        for (j = 0; j < i; j++) {
            DriveInfo *prev = &drive_list.drives[j];
            if (!prev->handler_name[0]) continue;
            if (prev->unit_number != drive->unit_number) continue;
            if (strcmp(prev->handler_name, drive->handler_name) != 0) continue;
            drive->scsi_supported = prev->scsi_supported;
            cached = TRUE;
            break;
        }

        if (!cached) {
            drive->scsi_supported = check_scsi_direct_support(
                drive->handler_name, drive->unit_number);
        }

        debug("  drives: SCSI support for %s unit %lu: %s%s\n",
              (LONG)drive->handler_name,
              (ULONG)drive->unit_number,
              (LONG)(drive->scsi_supported ? get_string(MSG_YES) : get_string(MSG_NO)),
              (LONG)(cached ? " (cached)" : ""));
    }
}

/*
 * Helper: Drop DOS devices that turned out not to be drives
 *
 * A row survives when anything disk-like was found for it: a parsed
 * FileSysStartupMsg (handler name), a matched volume, or successful
 * Info(). Plain I/O handlers like CON:, SER: or PRT: have none of
 * these and are removed here.
 */
static void prune_drive_list(void)
{
    ULONG i, kept = 0;

    for (i = 0; i < drive_list.count; i++) {
        DriveInfo *drive = &drive_list.drives[i];

        if (drive->is_valid || drive->handler_name[0] ||
            drive->volume_name[0]) {
            if (kept != i) {
                drive_list.drives[kept] = *drive;
            }
            kept++;
        } else {
            debug("  drives: Dropping '%s' (no FSSM, volume or Info)\n",
                  (LONG)drive->device_name);
        }
    }

    drive_list.count = kept;
}

/*
 * Helper: Sort drives alphabetically by DOS device name
 */
static void sort_drive_list(void)
{
    ULONG i, j;

    for (i = 0; i + 1 < drive_list.count; i++) {
        for (j = 0; j + 1 < drive_list.count - i; j++) {
            if (strcmp(drive_list.drives[j].device_name,
                       drive_list.drives[j + 1].device_name) > 0) {
                DriveInfo temp = drive_list.drives[j];
                drive_list.drives[j] = drive_list.drives[j + 1];
                drive_list.drives[j + 1] = temp;
            }
        }
    }
}

/*
 * Enumerate all drives
 */
void enumerate_drives(void)
{
    debug("  drives: Starting enumeration...\n");

    memset(&drive_list, 0, sizeof(drive_list));
    drive_page = 0;
    debug("  drives: Scan DosList...\n");

    /* First pass: Scan DosList for devices */
    scan_dos_list();

    debug("  drives: Match volumes...\n");
    /* Second pass: Match volumes to devices */
    match_volumes_to_drives();

    debug("  drives: Query details...\n");
    /* Third pass: Query detailed info */
    query_drive_details();

    debug("  drives: Prune non-drives...\n");
    /* Fourth pass: Drop entries with no drive evidence */
    prune_drive_list();

    debug("  drives: Sort drive list...\n");
    sort_drive_list();

    debug("  drives: Check SCSI-support...\n");
    /* Fifth pass: Check SCSI support */
    check_scsi_support_all();

    debug("  drives: Enumeration complete, found %ld drives\n", (LONG)drive_list.count);
}

/*
 * Refresh drive info for a specific drive
 */
void refresh_drive_info(ULONG index)
{
    (void)index;
    /* Re-enumerate for simplicity */
    enumerate_drives();
}

/*
 * Check if device is a floppy based on total block count.
 * Standard floppy sizes: DD = 1760 blocks, HD = 3520 blocks, ED = 7040 blocks.
 */
static BOOL is_floppy_device(ULONG total_blocks)
{
    return (total_blocks > 0 && total_blocks <= 7040);
}

static BOOL drive_has_media_evidence(const DriveInfo *drive)
{
    if (!drive || drive->disk_state == DISK_NO_DISK) {
        return FALSE;
    }

    if (is_floppy_device(get_geometry_total_blocks(drive))) {
        return TRUE;
    }

    return drive->volume_name[0] != '\0' || drive->has_info;
}

/*
 * Check if a disk is present in the drive using TD_CHANGESTATE.
 * Returns TRUE if a disk is present, FALSE otherwise.
 */
BOOL check_disk_present(ULONG index)
{
    DriveInfo *drive;
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    BYTE error;
    BOOL disk_present = FALSE;

    if (index >= (ULONG)drive_list.count) {
        return FALSE;
    }

    drive = &drive_list.drives[index];

    /* Check if we have device info */
    if (!drive->handler_name[0]) {
        return FALSE;
    }

    /* Only floppy-type devices have a cheap TD_CHANGESTATE probe here. */
    if (!is_floppy_device(get_geometry_total_blocks(drive))) {
        if (drive_has_media_evidence(drive)) {
            return TRUE;
        }

        drive->disk_state = DISK_NO_DISK;
        debug("  drives: %s has no volume or Info(), treating as no media\n",
              (LONG)drive->device_name);
        return FALSE;
    }

    /* Create message port */
    port = (struct MsgPort *)CreatePort(NULL, 0);
    if (!port) {
        return FALSE;
    }

    /* Create I/O request */
    io = (struct IOStdReq *)CreateExtIO(port, sizeof(struct IOStdReq));
    if (!io) {
        DeletePort(port);
        return FALSE;
    }

    /* Open the device */
    error = OpenDevice((CONST_STRPTR)drive->handler_name, drive->unit_number,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        DeleteExtIO((struct IORequest *)io);
        DeletePort(port);
        return FALSE;
    }

    /* Use TD_CHANGESTATE to check if disk is present */
    io->io_Command = TD_CHANGESTATE;
    error = DoIO((struct IORequest *)io);

    if (error == 0) {
        /* io_Actual is 0 if disk is present, non-zero if no disk */
        disk_present = (io->io_Actual == 0);
    }

    /* Update drive state based on result */
    if (disk_present) {
        if (drive->disk_state == DISK_NO_DISK) {
            drive->disk_state = DISK_UNKNOWN;
        }
    } else {
        drive->disk_state = DISK_NO_DISK;
        drive->volume_name[0] = '\0';
    }

    /* Clean up */
    CloseDevice((struct IORequest *)io);
    WaitTOF();
    DeleteExtIO((struct IORequest *)io);
    DeletePort(port);

    debug("  drives: TD_CHANGESTATE on %s unit %lu: disk %s\n",
          (LONG)drive->handler_name, (LONG)drive->unit_number,
          (LONG)(disk_present ? "present" : "not present"));

    return disk_present;
}

/*
 * Check whether an open device implements NSCMD_TD_READ64 via the
 * New Style Device query.
 */
static BOOL nsd_supports_read64(struct IOStdReq *io)
{
    struct NSDeviceQueryResult nsdqr;
    UWORD *cmd;
    BYTE error;

    memset(&nsdqr, 0, sizeof(nsdqr));

    io->io_Command = NSCMD_DEVICEQUERY;
    io->io_Data = &nsdqr;
    io->io_Length = sizeof(nsdqr);

    error = DoIO((struct IORequest *)io);
    if (error != 0 || io->io_Actual < 16 ||
        nsdqr.nsdqr_DeviceType != NSDEVTYPE_TRACKDISK ||
        !nsdqr.nsdqr_SupportedCommands) {
        return FALSE;
    }

    for (cmd = (UWORD *)nsdqr.nsdqr_SupportedCommands; *cmd; cmd++) {
        if (*cmd == NSCMD_TD_READ64) return TRUE;
    }
    return FALSE;
}

const char *drive_read_cmd_name(UWORD command)
{
    switch (command) {
        case CMD_READ:
            return "CMD_READ";
        case TD_READ64:
            return "TD_READ64";
        case NSCMD_TD_READ64:
            return "NSCMD_TD_READ64";
        default:
            return "UNKNOWN";
    }
}

static ULONG align_drive_transfer(ULONG size, ULONG block_size)
{
    if (block_size > 1) {
        size -= size % block_size;
    }
    return size;
}

static BOOL drive_buffer_matches_mask(APTR buffer, ULONG size, ULONG mask)
{
    ULONG start;
    ULONG end;
    ULONG disallowed;
    ULONG low_disallowed = 0;
    ULONG bit = 1;
    ULONG range_disallowed;

    if (!buffer || size == 0 || mask == 0) {
        return TRUE;
    }

    start = (ULONG)buffer;
    end = start + size - 1;
    if (end < start) {
        return FALSE;
    }

    /*
     * Low zero bits in de_Mask express buffer-start alignment, not that
     * every byte in the transfer range must keep those bits clear.  A mask
     * such as $7ffffffe accepts an even buffer start below 2 GB; checking
     * the last byte against bit 0 would reject every even-sized transfer.
     */
    disallowed = ~mask;
    while (bit != 0 && (mask & bit) == 0) {
        low_disallowed |= bit;
        bit <<= 1;
    }
    range_disallowed = disallowed & ~low_disallowed;

    return ((start & disallowed) == 0 && (end & range_disallowed) == 0);
}

static APTR alloc_masked_drive_buffer(ULONG size, ULONG flags, ULONG mask,
                                      const char *label, ULONG *used_flags)
{
    APTR buffer;

    buffer = AllocMem(size, flags | MEMF_CLEAR);
    if (!buffer) {
        debug("  drives: Buffer allocation failed using %s flags $%08lx\n",
              (LONG)label, (ULONG)flags);
        return NULL;
    }

    if (!drive_buffer_matches_mask(buffer, size, mask)) {
        debug("  drives: Buffer %s at $%08lx size %lu rejected by mask $%08lx\n",
              (LONG)label, (ULONG)buffer, (ULONG)size, (ULONG)mask);
        FreeMem(buffer, size);
        return NULL;
    }

    if (used_flags) {
        *used_flags = flags;
    }
    debug("  drives: Buffer %s at $%08lx size %lu flags $%08lx\n",
          (LONG)label, (ULONG)buffer, (ULONG)size, (ULONG)flags);

    return buffer;
}

static APTR alloc_drive_speed_buffer(const DriveInfo *drive, ULONG size,
                                     BOOL safe_retry, BOOL *can_retry,
                                     ULONG *used_flags)
{
    ULONG mask = 0;
    APTR buffer;

    if (can_retry) {
        *can_retry = FALSE;
    }
    if (drive->has_address_mask && drive->address_mask != 0) {
        mask = drive->address_mask;
    }

    if (safe_retry) {
        buffer = alloc_masked_drive_buffer(size, MEMF_CHIP | MEMF_PUBLIC,
                                           mask, "safe-chip-public",
                                           used_flags);
        if (buffer) return buffer;
        return alloc_masked_drive_buffer(size, MEMF_CHIP, mask, "safe-chip",
                                         used_flags);
    }

    if (drive->has_buf_mem_type && drive->buf_mem_type != 0) {
        if (can_retry && mask == 0 &&
            (drive->buf_mem_type & MEMF_CHIP) == 0) {
            *can_retry = TRUE;
        }
        buffer = alloc_masked_drive_buffer(size, drive->buf_mem_type, mask,
                                           "DosEnvec", used_flags);
        if (buffer) return buffer;

        if (can_retry) {
            *can_retry = FALSE;
        }
        debug("  drives: Falling back from DosEnvec buffer flags $%08lx\n",
              (ULONG)drive->buf_mem_type);
        buffer = alloc_masked_drive_buffer(size, MEMF_CHIP | MEMF_PUBLIC,
                                           mask, "fallback-chip-public",
                                           used_flags);
        if (buffer) return buffer;
        return alloc_masked_drive_buffer(size, MEMF_CHIP, mask,
                                         "fallback-chip", used_flags);
    }

    if (can_retry && mask == 0) {
        *can_retry = TRUE;
    }

    buffer = alloc_masked_drive_buffer(size, MEMF_FAST, mask, "fast",
                                       used_flags);
    if (buffer) return buffer;

    buffer = alloc_masked_drive_buffer(size, MEMF_ANY, mask, "any",
                                       used_flags);
    if (buffer) return buffer;

    if (mask != 0) {
        buffer = alloc_masked_drive_buffer(size, MEMF_CHIP | MEMF_PUBLIC,
                                           mask, "masked-chip-public",
                                           used_flags);
        if (buffer) return buffer;
        return alloc_masked_drive_buffer(size, MEMF_CHIP, mask, "masked-chip",
                                         used_flags);
    }

    return NULL;
}

/* Bound sequential reads to the partition when its geometry is known. */
static BOOL drive_speed_range(const DriveInfo *drive, ULONG block_size,
                              uint64_t *offset, ULONG *max_bytes)
{
    uint64_t available = 0;

    *offset = 0;
    if (drive->surfaces && drive->sectors_per_track) {
        uint64_t cylinder = (uint64_t)drive->surfaces *
                            drive->sectors_per_track;

        if (drive->high_cylinder < drive->low_cylinder ||
            cylinder > UINT64_MAX / block_size)
            return FALSE;
        cylinder *= block_size;
        if ((uint64_t)drive->high_cylinder + 1 > UINT64_MAX / cylinder)
            return FALSE;
        *offset = (uint64_t)drive->low_cylinder * cylinder;
        available = ((uint64_t)drive->high_cylinder + 1 -
                     drive->low_cylinder) * cylinder;
    } else if (get_geometry_total_blocks(drive)) {
        /* A filesystem block count can understate the raw range when its
         * blocks are larger. Using device block size is conservative. */
        available = (uint64_t)get_geometry_total_blocks(drive) * block_size;
    }
    if (available && available < *max_bytes)
        *max_bytes = (ULONG)available;
    *max_bytes = align_drive_transfer(*max_bytes, block_size);
    return *max_bytes >= block_size && *offset <= UINT64_MAX - *max_bytes;
}

/* No output or task/interrupt suppression belongs in the timed read loop. */
static BOOL measure_drive_sample(struct IOStdReq *io, APTR buffer,
                                 ULONG buffer_size, ULONG block_size,
                                 UWORD read_cmd, uint64_t offset,
                                 ULONG max_bytes, ULONG max_us,
                                 BOOL is_floppy, DriveSpeedSample *sample,
                                 BOOL *read_failed, ULONG *bytes_issued)
{
    struct EClockVal start, end;
    ULONG rate, end_rate;
    ULONG read_length = 0;
    uint64_t speed;
    BYTE error = 0;
    BOOL timer_failed = FALSE;

    memset(sample, 0, sizeof(*sample));
    sample->offset = offset;
    *read_failed = FALSE;
    *bytes_issued = 0;
    rate = read_benchmark_clock(&start);

    while (sample->bytes_read < max_bytes) {
        uint64_t position = offset + sample->bytes_read;

        if (!is_floppy && sample->read_count &&
            (sample->elapsed_us >= max_us ||
             (sample->read_count >= 2 &&
              sample->elapsed_us >= DRIVE_SPEED_TARGET_US)))
            break;

        read_length = buffer_size;
        if (!is_floppy && read_length > DRIVE_SPEED_FIRST_CHUNK &&
            block_size <= DRIVE_SPEED_FIRST_CHUNK &&
            (sample->read_count == 0 ||
             sample->elapsed_us > DRIVE_SPEED_TARGET_US / 4))
            read_length = DRIVE_SPEED_FIRST_CHUNK;
        if (read_length > max_bytes - sample->bytes_read)
            read_length = max_bytes - sample->bytes_read;
        read_length = align_drive_transfer(read_length, block_size);
        if (read_length < block_size) break;

        io->io_Command = read_cmd;
        io->io_Data = buffer;
        io->io_Length = read_length;
        io->io_Offset = (ULONG)position;
        io->io_Actual = (ULONG)(position >> 32);

        *bytes_issued += read_length;
        error = DoIO((struct IORequest *)io);
        end_rate = read_benchmark_clock(&end);
        /* A zero rate selects GetSysTime's microsecond representation. */
        if (end_rate != rate || end.ev_hi < start.ev_hi ||
            (end.ev_hi == start.ev_hi && end.ev_lo < start.ev_lo)) {
            timer_failed = TRUE;
            break;
        }
        sample->elapsed_us = EClock_Diff_in_ms(&start, &end, rate);
        if (error || io->io_Error || io->io_Actual != read_length) {
            *read_failed = TRUE;
            break;
        }

        sample->bytes_read += io->io_Actual;
        sample->read_count++;
        if (!sample->min_transfer || read_length < sample->min_transfer)
            sample->min_transfer = read_length;
        if (read_length > sample->max_transfer)
            sample->max_transfer = read_length;
    }

    /* Take the final timestamp before any diagnostics. Include loop work
     * between requests in this end-to-end device throughput measurement. */
    end_rate = read_benchmark_clock(&end);
    if (end_rate != rate || end.ev_hi < start.ev_hi ||
        (end.ev_hi == start.ev_hi && end.ev_lo < start.ev_lo))
        timer_failed = TRUE;
    if (timer_failed) {
        *read_failed = FALSE; /* Changing buffers cannot repair a timer. */
        debug("  drives: Invalid timer interval during speed measurement\n");
        return FALSE;
    }
    sample->elapsed_us = EClock_Diff_in_ms(&start, &end, rate);
    if (*read_failed) {
        uint64_t position = offset + sample->bytes_read;

        debug("  drives: Read failed at $%08lx:%08lx, requested %lu "
              "actual %lu, error %ld/%ld\n",
              (ULONG)(position >> 32), (ULONG)position,
              read_length, (ULONG)io->io_Actual,
              (LONG)error, (LONG)io->io_Error);
        return FALSE;
    }
    if (!sample->elapsed_us || !sample->bytes_read) {
        debug("  drives: No measurable read interval\n");
        return FALSE;
    }
    speed = (uint64_t)sample->bytes_read * 1000000ULL / sample->elapsed_us;
    sample->bytes_sec = speed > ULONG_MAX ? ULONG_MAX : (ULONG)speed;
    return sample->bytes_sec != 0;
}

static ULONG summarize_drive_speed(DriveSpeedResults *results)
{
    ULONG speeds[DRIVE_SPEED_SAMPLES];
    ULONG i, j, count = results->sample_count;

    if (!count) return 0;
    for (i = 0; i < count; i++) {
        ULONG speed = results->samples[i].bytes_sec;
        for (j = i; j && speeds[j - 1] > speed; j--)
            speeds[j] = speeds[j - 1];
        speeds[j] = speed;
    }
    results->min_bytes_sec = speeds[0];
    results->max_bytes_sec = speeds[count - 1];
    if (count & 1) return speeds[count / 2];
    return (ULONG)(((uint64_t)speeds[count / 2 - 1] +
                    speeds[count / 2]) / 2);
}

/* Measure sequential device reads; the GUI keeps just the median speed. */
ULONG measure_drive_speed(ULONG index)
{
    DriveInfo *drive;
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    APTR buffer = NULL;
    BOOL device_opened = FALSE;
    ULONG buffer_size = 0;
    ULONG block_size;
    ULONG total_read = 0;
    uint64_t total_elapsed = 0;
    ULONG bytes_per_sec = 0;
    ULONG max_test_bytes;
    ULONG bytes_left;
    ULONG wanted_samples;
    ULONG i;
    ULONG used_buffer_flags = 0;
    UWORD read_cmd = CMD_READ;
    uint64_t read_offset_bytes = 0;
    BYTE error;
    BOOL is_floppy;
    BOOL read_failed = FALSE;
    BOOL safe_buffer_retry = FALSE;
    BOOL can_retry_safe_buffer = FALSE;

    if (index >= (ULONG)drive_list.count) {
        debug("  drives: Invalid drive index %lu (count=%lu)\n",
              index, drive_list.count);
        return 0;
    }

    drive = &drive_list.drives[index];
    drive->speed_measured = FALSE;
    drive->speed_bytes_sec = 0;
    memset(&drive->speed_results, 0, sizeof(drive->speed_results));
    if (!benchmark_timer_available()) return 0;

    /* Check if we have device info */
    if (!drive->handler_name[0]) {
        debug("  drives: No handler name for speed test on %s\n",
              (LONG)drive->device_name);
        return 0;
    }

    if (!drive_has_media_evidence(drive)) {
        debug("  drives: No media evidence for speed test on %s\n",
              (LONG)drive->device_name);
        return 0;
    }

    /* Determine block size and buffer size based on device type */
    block_size = drive->bytes_per_block ? drive->bytes_per_block : 512;
    is_floppy = is_floppy_device(get_geometry_total_blocks(drive));
    if (is_floppy) {
        ULONG sectors_per_track = drive->sectors_per_track ?
                                  drive->sectors_per_track :
                                  DRIVE_SPEED_DD_SECTORS;

        /* Read three complete logical tracks.  Besides giving the test more
         * data, this includes both a side change and a cylinder step on a
         * normal two-sided floppy. */
        if (sectors_per_track > ULONG_MAX / block_size /
                                DRIVE_SPEED_FLOPPY_TRACKS)
            return 0;
        buffer_size = sectors_per_track * block_size;
        max_test_bytes = buffer_size * DRIVE_SPEED_FLOPPY_TRACKS;
        wanted_samples = 1;
    } else {
        buffer_size = DRIVE_SPEED_HARD_CHUNK;
        max_test_bytes = DRIVE_SPEED_MAX_BYTES;
        wanted_samples = DRIVE_SPEED_SAMPLES;
    }
    if (buffer_size < block_size) buffer_size = block_size;
    buffer_size = align_drive_transfer(buffer_size, block_size);
    if (buffer_size < block_size) buffer_size = block_size;
    if (drive->has_max_transfer && drive->max_transfer != 0 &&
        drive->max_transfer < buffer_size) {
        ULONG capped = align_drive_transfer(drive->max_transfer, block_size);

        if (capped >= block_size) {
            buffer_size = capped;
            debug("  drives: Capping speed read size to MaxTransfer %lu\n",
                  (ULONG)buffer_size);
        } else {
            debug("  drives: MaxTransfer %lu is smaller than block %lu\n",
                  (ULONG)drive->max_transfer, (ULONG)block_size);
            return 0;
        }
    }
    if (!drive_speed_range(drive, block_size, &read_offset_bytes,
                           &max_test_bytes)) {
        debug("  drives: Invalid speed-test address range\n");
        return 0;
    }
    if (buffer_size > max_test_bytes) buffer_size = max_test_bytes;
    if (wanted_samples > max_test_bytes / block_size)
        wanted_samples = max_test_bytes / block_size;
    bytes_left = max_test_bytes;

    /* Create message port */
    port = (struct MsgPort *)CreatePort(NULL, 0);
    if (!port) {
        debug("  drives: Failed to create message port\n");
        goto cleanup;
    }

    /* Create I/O request */
    io = (struct IOStdReq *)CreateExtIO(port, sizeof(struct IOStdReq));
    if (!io) {
        debug("  drives: Failed to create IO request\n");
        goto cleanup;
    }

    /* Open the device */
    debug("  drives: Opening device '%s' unit %ld\n",
          (LONG)drive->handler_name, (LONG)drive->unit_number);
    error = OpenDevice((CONST_STRPTR)drive->handler_name, drive->unit_number,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        debug("  drives: Failed to open device %s unit %ld (error %ld)\n",
              (LONG)drive->handler_name, (LONG)drive->unit_number, (LONG)error);
        goto cleanup;
    }
    device_opened = TRUE;

retry_speed_test:
    if (buffer) {
        FreeMem(buffer, buffer_size);
        buffer = NULL;
    }
    total_read = 0;
    bytes_per_sec = 0;
    read_failed = FALSE;
    read_cmd = CMD_READ;
    can_retry_safe_buffer = FALSE;
    used_buffer_flags = 0;
    memset(&drive->speed_results, 0, sizeof(drive->speed_results));
    /* Keep whole-run budgets across the one permitted buffer retry. */
    if (bytes_left < block_size || total_elapsed >= DRIVE_SPEED_TOTAL_US)
        goto cleanup;

    buffer = alloc_drive_speed_buffer(drive, buffer_size, safe_buffer_retry,
                                      &can_retry_safe_buffer,
                                      &used_buffer_flags);
    if (!buffer) {
        debug("  drives: Failed to allocate buffer\n");
        goto cleanup;
    }

    if (!drive_speed_range(drive, block_size, &read_offset_bytes,
                           &max_test_bytes))
        goto cleanup;

    /* CMD_READ takes a 32-bit byte offset; partitions starting beyond
     * 4 GB need one of the 64-bit read commands */
    if (read_offset_bytes + (uint64_t)max_test_bytes > 0xFFFFFFFFULL) {
        if (nsd_supports_read64(io)) {
            read_cmd = NSCMD_TD_READ64;
            debug("  drives: Using NSD TD_READ64 for offset beyond 4 GB\n");
        } else {
            read_cmd = TD_READ64;
            debug("  drives: Trying TD64 TD_READ64 for offset beyond 4 GB\n");
        }
    }

    debug("  drives: Speed test on %s unit %ld, cmd %s, chunk %lu max %lu target %lu us flags $%08lx at offset %lu MB ($%08lx:%08lx)\n",
          (LONG)drive->handler_name, (LONG)drive->unit_number,
          (LONG)drive_read_cmd_name(read_cmd),
          (ULONG)buffer_size, (ULONG)max_test_bytes,
          is_floppy ? 0UL : DRIVE_SPEED_TARGET_US,
          (ULONG)used_buffer_flags,
          (ULONG)(read_offset_bytes >> 20),
          (ULONG)(read_offset_bytes >> 32), (ULONG)read_offset_bytes);

    /* Untimed probe read; if the 64-bit command is not implemented,
     * fall back to measuring at the start of the device instead of
     * issuing reads at a wrapped 32-bit offset */
    if (read_cmd != CMD_READ) {
        io->io_Command = read_cmd;
        io->io_Data = buffer;
        io->io_Length = block_size;
        io->io_Offset = (ULONG)read_offset_bytes;
        io->io_Actual = (ULONG)(read_offset_bytes >> 32);

        debug("  drives: Probe %s offset $%08lx:%08lx len %lu\n",
              (LONG)drive_read_cmd_name(read_cmd),
              (ULONG)(read_offset_bytes >> 32), (ULONG)read_offset_bytes,
              (ULONG)block_size);

        error = DoIO((struct IORequest *)io);
        debug("  drives: Probe result error %ld io_Error %ld actual %lu\n",
              (LONG)error, (LONG)io->io_Error, (ULONG)io->io_Actual);
        if (error != 0 || io->io_Error != 0 ||
            io->io_Actual != block_size) {
            if (can_retry_safe_buffer && !safe_buffer_retry) {
                debug("  drives: 64-bit probe failed, retrying with safe buffer\n");
                safe_buffer_retry = TRUE;
                goto retry_speed_test;
            }
            debug("  drives: 64-bit read probe failed, reading at offset 0 instead\n");
            read_cmd = CMD_READ;
            read_offset_bytes = 0;
            drive->speed_results.offset_fallback = TRUE;
        }
    }

    /* Warm up floppy by doing an untimed read to get the head on track */
    if (is_floppy) {
        io->io_Command = CMD_READ;
        io->io_Data = buffer;
        io->io_Length = block_size;
        io->io_Offset = (ULONG)read_offset_bytes;

        error = DoIO((struct IORequest *)io);
        if (error != 0) {
            debug("  drives: Warm-up read error %ld (ignoring)\n", (LONG)error);
        }
    }

    drive->speed_results.buffer_flags = used_buffer_flags;
    drive->speed_results.buffer_type = TypeOfMem(buffer);
    drive->speed_results.read_command = read_cmd;
    drive->speed_results.safe_buffer_retry = safe_buffer_retry;

    for (i = 0; i < wanted_samples; i++) {
        DriveSpeedSample *sample = &drive->speed_results.samples[i];
        ULONG sample_bytes, sample_us, issued;
        BOOL valid;

        if (bytes_left < block_size || total_elapsed >= DRIVE_SPEED_TOTAL_US)
            break;
        /* Reserve a share of the remaining range for each later sample.
         * Start the next sample immediately after the last successful read. */
        sample_bytes = (max_test_bytes - total_read) / (wanted_samples - i);
        if (sample_bytes > bytes_left) sample_bytes = bytes_left;
        sample_bytes = align_drive_transfer(sample_bytes, block_size);
        if (sample_bytes < block_size) break;
        sample_us = DRIVE_SPEED_TOTAL_US - (ULONG)total_elapsed;
        if (sample_us > DRIVE_SPEED_MAX_US) sample_us = DRIVE_SPEED_MAX_US;

        valid = measure_drive_sample(io, buffer, buffer_size, block_size,
                                     read_cmd, read_offset_bytes + total_read,
                                     sample_bytes, sample_us, is_floppy,
                                     sample, &read_failed, &issued);
        bytes_left -= issued;
        total_elapsed += sample->elapsed_us;
        if (!valid) {
            if (read_failed && can_retry_safe_buffer && !safe_buffer_retry) {
                debug("  drives: Read failed, restarting with safe buffer\n");
                safe_buffer_retry = TRUE;
                goto retry_speed_test;
            }
            goto cleanup;
        }
        total_read += sample->bytes_read;
        drive->speed_results.sample_count++;
    }

    bytes_per_sec = summarize_drive_speed(&drive->speed_results);
    drive->speed_bytes_sec = bytes_per_sec;
    drive->speed_measured = bytes_per_sec != 0;
    for (i = 0; i < drive->speed_results.sample_count; i++) {
        const DriveSpeedSample *sample = &drive->speed_results.samples[i];

        debug("  drives: Sample %lu at $%08lx:%08lx: %lu bytes in %lu us, "
              "%lu reads of %lu..%lu bytes = %lu B/s\n",
              i + 1, (ULONG)(sample->offset >> 32), (ULONG)sample->offset,
              sample->bytes_read, sample->elapsed_us, sample->read_count,
              sample->min_transfer, sample->max_transfer, sample->bytes_sec);
    }
    debug("  drives: Median %lu B/s, range %lu..%lu B/s, %lu samples; "
          "buffer type $%08lx, requested $%08lx\n",
          bytes_per_sec, drive->speed_results.min_bytes_sec,
          drive->speed_results.max_bytes_sec, drive->speed_results.sample_count,
          drive->speed_results.buffer_type, used_buffer_flags);

cleanup:
    if (buffer) FreeMem(buffer, buffer_size);
    if (device_opened) {
        if (is_floppy) {
            /* Reads start a floppy motor automatically, but closing the
             * device does not stop it.  Turn it off on every exit path. */
            io->io_Command = TD_MOTOR;
            io->io_Data = NULL;
            io->io_Length = 0;
            io->io_Offset = 0;
            io->io_Actual = 0;
            io->io_Flags = 0;

            error = DoIO((struct IORequest *)io);
            debug("  drives: Motor off on %s unit %ld: error %ld/%ld\n",
                  (LONG)drive->handler_name, (LONG)drive->unit_number,
                  (LONG)error, (LONG)io->io_Error);
        }
        CloseDevice((struct IORequest *)io);
        WaitTOF();
    }
    if (io) DeleteExtIO((struct IORequest *)io);
    if (port) DeletePort(port);

    if (!drive->speed_measured) {
        /* Never publish partial samples or leave stale results after failure. */
        memset(&drive->speed_results, 0, sizeof(drive->speed_results));
    }

    return bytes_per_sec;
}

/*
 * Draw drives data area (buttons, info panel, action buttons - no title)
 */
static void format_drive_speed(const DriveInfo *drive, char *buffer,
                               size_t size)
{
    if (drive->speed_measured) {
        ULONG speed = drive->speed_bytes_sec;
        if (speed >= 1000000) {
            snprintf(buffer, size, "%lu.%lu MB/s",
                     (unsigned long)(speed / 1000000),
                     (unsigned long)((speed % 1000000) / 100000));
        } else if (speed >= 10000) {
            snprintf(buffer, size, "%lu KB/s",
                     (unsigned long)(speed / 1000));
        } else {
            snprintf(buffer, size, "%lu B/s", (unsigned long)speed);
        }
    } else {
        snprintf(buffer, size, "%s", get_string(MSG_DASH_PLACEHOLDER));
    }
}

static void draw_drive_value(WORD y, const char *value);

static void draw_drive_speed_row(void)
{
    DriveInfo *drive;
    char buffer[64];

    if (app->selected_drive < 0 ||
        app->selected_drive >= (LONG)drive_list.count) {
        return;
    }

    drive = &drive_list.drives[app->selected_drive];
    format_drive_speed(drive, buffer, sizeof(buffer));

    draw_drive_value(175, buffer);
}

static void draw_drive_value(WORD y, const char *value)
{
    struct RastPort *rp = app->rp;
    WORD top = y - rp->TxBaseline;

    /* Clear only this font row: a taller rectangle erases the previous
     * row's descenders with the drive view's nine-pixel line spacing. */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, DRIVE_VALUE_X, top, DRIVE_VALUE_MAX_X,
             top + rp->TxHeight - 1);
    SetAPen(rp, COLOR_HIGHLIGHT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_text_clipped(DRIVE_VALUE_X, y, value,
                      DRIVE_VALUE_MAX_X - DRIVE_VALUE_X);
}

static void draw_drive_values(void)
{
    DriveInfo *drive;
    char buffer[64];
    const char *value;
    WORD y = 40;

    if (app->selected_drive < 0 ||
        app->selected_drive >= (LONG)drive_list.count) {
        return;
    }

    drive = &drive_list.drives[app->selected_drive];

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->disk_errors);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->unit_number);
    draw_drive_value(y, buffer);
    y += 9;

    if (drive->disk_state == DISK_NO_DISK) {
        value = get_string(MSG_DASH_PLACEHOLDER);
    } else {
        value = get_disk_state_string(drive->disk_state);
    }
    draw_drive_value(y, value);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->total_blocks);
    draw_drive_value(y, buffer);
    y += 9;

    if (drive->disk_state == DISK_NO_DISK) {
        value = get_string(MSG_DASH_PLACEHOLDER);
    } else {
        snprintf(buffer, sizeof(buffer), "%lu",
                 (unsigned long)drive->blocks_used);
        value = buffer;
    }
    draw_drive_value(y, value);
    y += 9;

    if (drive->disk_state == DISK_NO_DISK) {
        value = get_string(MSG_DASH_PLACEHOLDER);
    } else {
        format_block_size_display(drive, buffer, sizeof(buffer));
        value = buffer;
    }
    draw_drive_value(y, value);
    y += 9;

    if (drive->disk_state == DISK_NO_DISK) {
        value = get_string(MSG_DISK_NO_DISK_INSERTED);
    } else {
        format_filesystem_display(drive, buffer, sizeof(buffer));
        value = buffer;
    }
    draw_drive_value(y, value);
    y += 9;

    if (drive->disk_state == DISK_NO_DISK || !drive->volume_name[0]) {
        value = get_string(MSG_DASH_PLACEHOLDER);
    } else {
        value = drive->volume_name;
    }
    draw_drive_value(y, value);
    y += 9;

    if (drive->handler_name[0]) {
        value = drive->handler_name;
    } else {
        value = get_string(MSG_DASH_PLACEHOLDER);
    }
    draw_drive_value(y, value);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->surfaces);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->sectors_per_track);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->reserved_blocks);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->low_cylinder);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->high_cylinder);
    draw_drive_value(y, buffer);
    y += 9;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->num_buffers);
    draw_drive_value(y, buffer);
    y += 9;

    format_drive_speed(drive, buffer, sizeof(buffer));
    draw_drive_value(y, buffer);
}

static void draw_drive_select_buttons(void)
{
    int i;

    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_DRV_DRIVE_BASE &&
            buttons[i].id < BTN_DRV_DRIVE_BASE + MAX_DRIVES) {
            buttons[i].pressed = (app->selected_drive ==
                                  (LONG)(buttons[i].id -
                                         BTN_DRV_DRIVE_BASE));
            draw_button(&buttons[i]);
        }
    }
}

static void draw_drive_action_buttons(void)
{
    Button *btn;

    btn = find_button(BTN_DRV_EXIT);
    if (btn) draw_button(btn);
    btn = find_button(BTN_DRV_SCSI);
    if (btn) draw_button(btn);
    btn = find_button(BTN_DRV_SPEED);
    if (btn) draw_button(btn);

    /* Draw pager buttons (only added when the list spans pages) */
    btn = find_button(BTN_DRV_PAGE_PREV);
    if (btn) draw_button(btn);
    btn = find_button(BTN_DRV_PAGE_NEXT);
    if (btn) draw_button(btn);
}

static void draw_drives_data(BOOL full_redraw)
{
    struct RastPort *rp = app->rp;
    WORD y;
    DriveInfo *drive;
    char buffer[64];

    /* Draw drive selection buttons on left */
    draw_drive_select_buttons();

    if (full_redraw) {
        /* Draw drive info panel with 3D border */
        draw_panel(100, 28, 520, 152, NULL);
    } else {
        draw_drive_values();
        draw_drive_action_buttons();
        return;
    }

    if (app->selected_drive < 0 || app->selected_drive >= (LONG)drive_list.count) {
        SetAPen(rp, COLOR_TEXT);
        Move(rp, 250, 120);
        Text(rp, (CONST_STRPTR)get_string(MSG_DRIVES_NO_DRIVES_FOUND), strlen(get_string(MSG_DRIVES_NO_DRIVES_FOUND)));
    } else {
        drive = &drive_list.drives[app->selected_drive];
        y = 40;

        /* Number of disk errors */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->disk_errors);
        draw_label_value(120, y, get_string(MSG_DISK_ERRORS), buffer, 224);
        y += 9;

        /* Unit number */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->unit_number);
        draw_label_value(120, y, get_string(MSG_UNIT_NUMBER), buffer, 224);
        y += 9;

        /* Disk state */
        if (drive->disk_state == DISK_NO_DISK) {
            draw_label_value(120, y, get_string(MSG_DISK_STATE), get_string(MSG_DASH_PLACEHOLDER), 224);
        } else {
            draw_label_value(120, y, get_string(MSG_DISK_STATE),
                             get_disk_state_string(drive->disk_state), 224);
        }
        y += 9;

        /* Total blocks */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->total_blocks);
        draw_label_value(120, y, get_string(MSG_TOTAL_BLOCKS), buffer, 224);
        y += 9;

        /* Blocks used */
        if (drive->disk_state == DISK_NO_DISK) {
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_DASH_PLACEHOLDER));
        } else {
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->blocks_used);
        }
        draw_label_value(120, y, get_string(MSG_BLOCKS_USED), buffer, 224);
        y += 9;

        /* Bytes per block */
        if (drive->disk_state == DISK_NO_DISK) {
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_DASH_PLACEHOLDER));
        } else {
            format_block_size_display(drive, buffer, sizeof(buffer));
        }
        draw_label_value(120, y, get_string(MSG_BYTES_PER_BLOCK), buffer, 224);
        y += 9;

        /* Filesystem type */
        if (drive->disk_state == DISK_NO_DISK) {
            draw_label_value(120, y, get_string(MSG_DISK_TYPE), get_string(MSG_DISK_NO_DISK_INSERTED), 224);
        } else {
            format_filesystem_display(drive, buffer, sizeof(buffer));
            draw_label_value(120, y, get_string(MSG_DISK_TYPE),
                             buffer, 224);
        }
        y += 9;

        /* Volume name */
        draw_label_value(120, y, get_string(MSG_VOLUME_NAME),
                         (drive->disk_state == DISK_NO_DISK || !drive->volume_name[0]) ? get_string(MSG_DASH_PLACEHOLDER) : drive->volume_name, 224);
        y += 9;

        /* Device name */
        draw_label_value(120, y, get_string(MSG_DEVICE_NAME),
                         drive->handler_name[0] ? drive->handler_name : get_string(MSG_DASH_PLACEHOLDER), 224);
        y += 9;

        /* Surfaces */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->surfaces);
        draw_label_value(120, y, get_string(MSG_SURFACES), buffer, 224);
        y += 9;

        /* Sectors per side */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->sectors_per_track);
        draw_label_value(120, y, get_string(MSG_SECTORS_PER_SIDE), buffer, 224);
        y += 9;

        /* Reserved blocks */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->reserved_blocks);
        draw_label_value(120, y, get_string(MSG_RESERVED_BLOCKS), buffer, 224);
        y += 9;

        /* Lowest cylinder */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->low_cylinder);
        draw_label_value(120, y, get_string(MSG_LOWEST_CYLINDER), buffer, 224);
        y += 9;

        /* Highest cylinder */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->high_cylinder);
        draw_label_value(120, y, get_string(MSG_HIGHEST_CYLINDER), buffer, 224);
        y += 9;

        /* Number of buffers */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->num_buffers);
        draw_label_value(120, y, get_string(MSG_NUM_BUFFERS), buffer, 224);
        y += 9;

        format_drive_speed(drive, buffer, sizeof(buffer));
        draw_label_value(120, y, get_string(MSG_SPEED), buffer, 224);
    }

    /* Draw bottom buttons */
    draw_drive_action_buttons();
}

/*
 * Draw drives view
 */
void draw_drives_view(void)
{
    /* Draw title panel */
    draw_panel(100, 0, 520, 24, NULL);

    draw_text_centered(100, 14, 520, get_string(MSG_DRIVES_INFO), COLOR_TEXT);

    /* Draw data area with full panel borders */
    draw_drives_data(TRUE);
}

/*
 * Update buttons for Drives view
 */
void drives_view_update_buttons(void)
{
    BOOL scsi_enabled = FALSE;
    BOOL speed_enabled = FALSE;
    ULONG i, first;
    ULONG max_page;
    WORD y = 28;

    /* Drive selection buttons, one page at a time */
    max_page = drive_list.count ?
               (drive_list.count - 1) / DRIVES_PER_PAGE : 0;
    if (drive_page > max_page) drive_page = max_page;
    first = drive_page * DRIVES_PER_PAGE;

    for (i = first;
         i < drive_list.count && i < first + DRIVES_PER_PAGE; i++) {
        add_button(10, y, 70, 12,
                   drive_list.drives[i].device_name,
                   (ButtonID)(BTN_DRV_DRIVE_BASE + i), TRUE);
        y += 14;
    }

    /* Pager below the drive buttons when the list spans pages */
    if (drive_list.count > DRIVES_PER_PAGE) {
        add_button(10, 170, 33, 12, "<", BTN_DRV_PAGE_PREV,
                   drive_page > 0);
        add_button(47, 170, 33, 12, ">", BTN_DRV_PAGE_NEXT,
                   drive_page < max_page);
    }

    /* Check capabilities of selected drive */
    if (app->selected_drive >= 0 &&
        app->selected_drive < (LONG)drive_list.count) {
        DriveInfo *drive = &drive_list.drives[app->selected_drive];
        scsi_enabled = drive->scsi_supported;
        /* Speed needs a disk and an exec device to read from; volume-only
         * entries (RAM:, mounts without a parseable FSSM) have neither */
        speed_enabled = (drive->handler_name[0] != '\0' &&
                         drive_has_media_evidence(drive));
    }

    /* Action buttons */
    add_button(100, 188, 52, 12,
               get_string(MSG_BTN_SCSI), BTN_DRV_SCSI, scsi_enabled);
    add_button(160, 188, 52, 12,
               get_string(MSG_BTN_SPEED), BTN_DRV_SPEED, speed_enabled);
    add_button(220, 188, 52, 12,
               get_string(MSG_BTN_EXIT), BTN_DRV_EXIT, TRUE);
}

/*
 * Redraw the drives view after a page flip: clear the drive button
 * column first so a shorter page leaves no stale button images behind.
 */
static void flip_drive_page(void)
{
    SetAPen(app->rp, COLOR_BACKGROUND);
    RectFill(app->rp, 10, 28, 79, 166);

    update_button_states();
    draw_drives_view();
}

/*
 * Handle button press for Drives view
 */
void drives_view_handle_button(ButtonID id)
{
    switch (id) {
        case BTN_DRV_EXIT:
            switch_to_view(VIEW_MAIN);
            break;

        case BTN_DRV_SCSI:
            if (app->selected_drive >= 0 &&
                app->selected_drive < (LONG)drive_list.count) {
                DriveInfo *drive = &drive_list.drives[app->selected_drive];
                scan_scsi_devices(drive->handler_name, drive->unit_number);
                switch_to_view(VIEW_SCSI);
            }
            break;

        case BTN_DRV_SPEED:
            if (app->selected_drive >= 0 &&
                app->selected_drive < (LONG)drive_list.count) {
                show_status_overlay(get_string(MSG_MEASURING_SPEED));
                measure_drive_speed(app->selected_drive);
                hide_status_overlay();
                draw_drive_speed_row();
            }
            break;

        case BTN_DRV_PAGE_PREV:
            if (drive_page > 0) {
                drive_page--;
                flip_drive_page();
            }
            break;

        case BTN_DRV_PAGE_NEXT:
            if ((drive_page + 1) * DRIVES_PER_PAGE < drive_list.count) {
                drive_page++;
                flip_drive_page();
            }
            break;

        default:
            /* Check for drive selection buttons */
            if (id >= BTN_DRV_DRIVE_BASE &&
                id < BTN_DRV_DRIVE_BASE + MAX_DRIVES) {
                ULONG drive_index = id - BTN_DRV_DRIVE_BASE;
                app->selected_drive = drive_index;
                /* Check if disk is present when selecting a drive */
                check_disk_present(drive_index);
                /* Only redraw data area, not the entire screen */
                update_button_states();
                draw_drives_data(FALSE);
            }
            break;
    }
}
