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

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include "xsysinfo.h"
#include "drives.h"
#include "scsi.h"
#include "gui.h"
#include "benchmark.h"
#include "locale_str.h"
#include "debug.h"
#include <limits.h>

/* Global drive list */
DriveList drive_list;

/* External references */
extern AppContext *app;
extern Button buttons[];
extern int num_buttons;

/* DOS type identifiers */
/* ID_DOS_DISK and ID_FFS_DISK are defined in dos/dos.h */
#define ID_INTER_DOS    0x444F5302  /* 'DOS\2' */
#define ID_INTER_FFS    0x444F5303  /* 'DOS\3' */
#define ID_DC_DOS       0x444F5304  /* 'DOS\4' */
#define ID_DC_FFS       0x444F5305  /* 'DOS\5' */
#define ID_SFS_BE       0x53465300  /* 'SFS\0' */
#define ID_PFS          0x50465300  /* 'PFS\0' */

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
        case ID_SFS_BE:
            return FS_SFS;
        case ID_PFS:
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
        case FS_SFS:        return get_string(MSG_SFS);
        case FS_PFS:        return get_string(MSG_PFS);
        default:            return get_string(MSG_UNKNOWN_FS);
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

/*
 * Get block size to display (adjusts for OFS overhead)
 */
ULONG get_display_block_size(const DriveInfo *drive)
{
    ULONG block_size;

    if (!drive) return 0;

    block_size = drive->bytes_per_block;
    if (drive->fs_type == FS_OFS && block_size >= 24) {
        block_size -= 24;
    }

    return block_size;
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

/* Helpers */
static BOOL is_floppy_device(ULONG total_blocks);

/*
 * Helper: Scan DosList for devices and populate drive list
 */
static void scan_dos_list(void)
{
    struct DosList *dol;

    debug("  drives: Locking DosList...\n");
    dol = LockDosList(LDF_DEVICES | LDF_READ);
    debug("  drives: DosList locked\n");

    while ((dol = NextDosEntry(dol, LDF_DEVICES)) != NULL) {
        if (drive_list.count >= MAX_DRIVES) break;

        DriveInfo *drive = &drive_list.drives[drive_list.count];
        drive->is_valid = FALSE;

        /* Get device name */
        bstr_to_cstr(dol->dol_Name, drive->device_name, sizeof(drive->device_name) - 2);
        strcat(drive->device_name, ":");

        debug("  drives: Found device '%s'\n", (LONG)drive->device_name);

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
                    drive->num_buffers = de->de_NumBuffers;

                    /* Get DOS type if available (de_DosType is at index 16) */
                    if (de->de_TableSize >= 16) {
                        drive->dos_type = de->de_DosType;
                        drive->fs_type = identify_filesystem(de->de_DosType);
                    }

                    /* Calculate total blocks */
                    drive->total_blocks = (drive->high_cylinder - drive->low_cylinder + 1) *
                                          drive->surfaces * drive->sectors_per_track;

                    drive->is_valid = TRUE;
                }
            }
        }

        /* Mark as no disk if we couldn't get FSSM info */
        if (!drive->is_valid) {
            drive->disk_state = DISK_NO_DISK;
        }

        if (drive->is_valid || drive->handler_name[0]) {
            drive_list.count++;
        }
    }

    debug("  drives: Unlocking DosList...\n");
    UnLockDosList(LDF_DEVICES | LDF_READ);
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
    dev_dol = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dev_dol = NextDosEntry(dev_dol, LDF_DEVICES)) != NULL) {
        char dev_name[32];
        bstr_to_cstr(dev_dol->dol_Name, dev_name, sizeof(dev_name) - 2);
        strcat(dev_name, ":");

        /* Find matching drive in our list */
        for (i = 0; i < drive_list.count; i++) {
            if (strcmp(drive_list.drives[i].device_name, dev_name) == 0) {
                dev_tasks[i] = dev_dol->dol_Task;
                break;
            }
        }
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);

    /* Now scan volumes and match by task pointer */
    debug("  drives: Looking up volume names...\n");
    dol = LockDosList(LDF_VOLUMES | LDF_READ);
    while ((dol = NextDosEntry(dol, LDF_VOLUMES)) != NULL) {
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
    UnLockDosList(LDF_VOLUMES | LDF_READ);
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
        BOOL is_floppy = is_floppy_device(drive->total_blocks);
        BOOL has_volume = drive->volume_name[0] != '\0';
        BPTR lock;

        /* Avoid hanging on empty floppies; require a volume to be present */
        if (!has_volume && is_floppy) continue;
        /* Skip clearly invalid entries */
        if (!drive->is_valid && !has_volume) continue;

        debug("  drives: Trying Info() on '%s'\n", (LONG)drive->device_name);
        lock = Lock((CONST_STRPTR)drive->device_name, ACCESS_READ);
        if (!lock) {
            debug("  drives: Lock failed on '%s'\n", (LONG)drive->device_name);
            if (drive->disk_state == DISK_OK) {
                drive->disk_state = DISK_NO_DISK;
            }
            continue;
        }

        if (Info(lock, info)) {
            drive->total_blocks = info->id_NumBlocks;
            drive->blocks_used = info->id_NumBlocksUsed;
            /* Only update block size if not set by DosEnvec (which gives physical size) */
            if (drive->bytes_per_block == 0) {
                drive->bytes_per_block = info->id_BytesPerBlock;
            }
            drive->dos_type = info->id_DiskType;
            drive->fs_type = identify_filesystem(info->id_DiskType);
            drive->disk_errors = info->id_NumSoftErrors;

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
 */
static void check_scsi_support_all(void)
{
    ULONG i;

    for (i = 0; i < drive_list.count; i++) {
        DriveInfo *drive = &drive_list.drives[i];

        if (!drive->handler_name[0]) continue;

        drive->scsi_supported = check_scsi_direct_support(
            drive->handler_name, drive->unit_number);
        debug("  drives: SCSI support for %s: %s\n",
              (LONG)drive->handler_name,
              (LONG)(drive->scsi_supported ? get_string(MSG_YES) : get_string(MSG_NO)));
    }
}

/*
 * Enumerate all drives
 */
void enumerate_drives(void)
{
    debug("  drives: Starting enumeration...\n");

    memset(&drive_list, 0, sizeof(drive_list));

    /* First pass: Scan DosList for devices */
    scan_dos_list();

    /* Second pass: Match volumes to devices */
    match_volumes_to_drives();

    /* Third pass: Query detailed info */
    query_drive_details();

    /* Fourth pass: Check SCSI support */
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

    /* Only check floppy-type devices */
    if (!is_floppy_device(drive->total_blocks)) {
        /* Non-floppy devices are assumed to have media present */
        return TRUE;
    }

    /* Create message port */
    port = CreateMsgPort();
    if (!port) {
        return FALSE;
    }

    /* Create I/O request */
    io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
    if (!io) {
        DeleteMsgPort(port);
        return FALSE;
    }

    /* Open the device */
    error = OpenDevice((CONST_STRPTR)drive->handler_name, drive->unit_number,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        DeleteIORequest((struct IORequest *)io);
        DeleteMsgPort(port);
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
    if (!disk_present) {
        drive->disk_state = DISK_NO_DISK;
        drive->volume_name[0] = '\0';
    }

    /* Clean up */
    CloseDevice((struct IORequest *)io);
    WaitTOF();
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);

    debug("  drives: TD_CHANGESTATE on %s unit %ld: disk %s\n",
          (LONG)drive->handler_name, (LONG)drive->unit_number,
          (LONG)(disk_present ? "present" : "not present"));

    return disk_present;
}

/*
 * Measure drive speed (bytes/second)
 */
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
    uint64_t start_time, end_time, elapsed;
    ULONG bytes_per_sec = 0;
    ULONG num_reads;
    ULONG read_offset;
    uint64_t read_offset_bytes = 0;
    ULONG i;
    BYTE error;
    BOOL is_floppy;

    if (index >= (ULONG)drive_list.count) {
        debug("  drives: Invalid drive index %ld (count=%ld)\n",
              (LONG)index, (LONG)drive_list.count);
        return 0;
    }

    drive = &drive_list.drives[index];

    /* Check if we have device info */
    if (!drive->handler_name[0]) {
        debug("  drives: No handler name for speed test on %s\n",
              (LONG)drive->device_name);
        /* Mark as measured with 0 speed so user sees it was attempted */
        drive->speed_measured = TRUE;
        drive->speed_bytes_sec = 0;
        return 0;
    }

    /* Determine block size and buffer size based on device type */
    block_size = drive->bytes_per_block ? drive->bytes_per_block : 512;
    is_floppy = is_floppy_device(drive->total_blocks);
    if (is_floppy) {
        /* Floppy: read small amount (one track worth) */
        buffer_size = 11 * 512;  /* 11 sectors * 512 bytes */
        num_reads = 2;
    } else {
        /* Hard drive: read larger blocks */
        buffer_size = 256 * 1024;  /* 256 KB */
        num_reads = 8;
    }
    if (block_size == 0) block_size = 512;
    if (buffer_size < block_size) buffer_size = block_size;
    if (block_size > 1) buffer_size -= buffer_size % block_size;

    /* Create message port */
    port = CreateMsgPort();
    if (!port) {
        debug("  drives: Failed to create message port\n");
        goto cleanup;
    }

    /* Create I/O request */
    io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
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

    /* Allocate buffer - try fast memory first for hard drives, fall back to any */
    buffer = AllocMem(buffer_size, MEMF_FAST | MEMF_CLEAR);
    if (!buffer) {
        buffer = AllocMem(buffer_size, MEMF_ANY | MEMF_CLEAR);
    }

    if (!buffer) {
        debug("  drives: Failed to allocate buffer\n");
        goto cleanup;
    }

    /* Calculate read offset - start from low cylinder, clamp to 32-bit */
    if (drive->surfaces && drive->sectors_per_track) {
        read_offset_bytes = (uint64_t)drive->low_cylinder *
                            (uint64_t)drive->surfaces *
                            (uint64_t)drive->sectors_per_track *
                            (uint64_t)block_size;
        if (block_size > 1 && read_offset_bytes > 0) {
            read_offset_bytes -= read_offset_bytes % block_size;
        }
    } else {
        debug("  drives: Missing geometry, defaulting read offset to 0\n");
    }

    if (read_offset_bytes > (uint64_t)(ULONG_MAX - buffer_size)) {
        uint64_t limit = (uint64_t)(ULONG_MAX - buffer_size);
        if (block_size > 1) limit -= limit % block_size;
        read_offset_bytes = limit;
    }

    read_offset = (ULONG)read_offset_bytes;

    debug("  drives: Speed test on %s unit %ld, %ld reads of %ld bytes at offset %ld\n",
          (LONG)drive->handler_name, (LONG)drive->unit_number,
          (LONG)num_reads, (LONG)buffer_size, (LONG)read_offset);

    /* Warm up floppy by doing an untimed read to get the head on track */
    if (is_floppy) {
        io->io_Command = CMD_READ;
        io->io_Data = buffer;
        io->io_Length = block_size;
        io->io_Offset = read_offset;

        error = DoIO((struct IORequest *)io);
        if (error != 0) {
            debug("  drives: Warm-up read error %ld (ignoring)\n", (LONG)error);
        }
    }

    /* Get start time */
    start_time = get_timer_ticks();

    /* Perform reads */
    for (i = 0; i < num_reads; i++) {
        io->io_Command = CMD_READ;
        io->io_Data = buffer;
        io->io_Length = buffer_size;
        io->io_Offset = read_offset + (i * buffer_size);

        error = DoIO((struct IORequest *)io);
        if (error != 0) {
            debug("  drives: Read error %ld at iteration %ld\n", (LONG)error, (LONG)i);
            break;
        }
        total_read += io->io_Actual;
    }

    /* Get end time */
    end_time = get_timer_ticks();

    /* Calculate speed */
    elapsed = end_time - start_time;
    if (elapsed > 0 && total_read > 0) {
        /* Timer ticks are in microseconds */
        bytes_per_sec = (ULONG)(((uint64_t)total_read * 1000000ULL) / elapsed);
        drive->speed_bytes_sec = bytes_per_sec;
        drive->speed_measured = TRUE;
    } else {
        drive->speed_bytes_sec = 0;
        drive->speed_measured = FALSE;
    }

    debug("  drives: Read %ld bytes in %ld us = %ld bytes/sec\n",
          (LONG)total_read, (LONG)elapsed, (LONG)bytes_per_sec);

cleanup:
    if (buffer) FreeMem(buffer, buffer_size);
    if (device_opened) {
        CloseDevice((struct IORequest *)io);
        WaitTOF();
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);

    if (!device_opened) {
        /* If we failed to open or allocate, ensure marked as failed */
        drive->speed_measured = FALSE;
        drive->speed_bytes_sec = 0;
    }

    return bytes_per_sec;
}

/*
 * Draw drives view
 */
void draw_drives_view(void)
{
    struct RastPort *rp = app->rp;
    WORD y;
    DriveInfo *drive;
    char buffer[64];
    Button *btn;
    int i;

    /* Draw title panel */
    draw_panel(100, 0, 520, 24, NULL);

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 250, 14);
    Text(rp, (CONST_STRPTR)get_string(MSG_DRIVES_INFO), strlen(get_string(MSG_DRIVES_INFO)));

    /* Draw drive selection buttons on left */
    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_DRV_DRIVE_BASE &&
            buttons[i].id < BTN_DRV_DRIVE_BASE + MAX_DRIVES) {
            buttons[i].pressed = (app->selected_drive ==
                                  (LONG)(buttons[i].id - BTN_DRV_DRIVE_BASE));
            draw_button(&buttons[i]);
        }
    }

    /* Draw drive info panel on right */
    draw_panel(100, 28, 520, 152, NULL);

    if (app->selected_drive < 0 || app->selected_drive >= (LONG)drive_list.count) {
        SetAPen(rp, COLOR_TEXT);
        Move(rp, 250, 12);
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

        /* Total blocks - always show since it's a drive geometry property */
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
            ULONG display_block_size = get_display_block_size(drive);
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)display_block_size);
        }
        draw_label_value(120, y, get_string(MSG_BYTES_PER_BLOCK), buffer, 224);
        y += 9;

        /* Filesystem type */
        if (drive->disk_state == DISK_NO_DISK) {
            draw_label_value(120, y, get_string(MSG_DISK_TYPE), get_string(MSG_DISK_NO_DISK_INSERTED), 224);
        } else {
            draw_label_value(120, y, get_string(MSG_DISK_TYPE),
                             get_filesystem_string(drive->fs_type), 224);
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

        /* Speed - display in appropriate units */
        if (drive->speed_measured) {
            ULONG speed = drive->speed_bytes_sec;
            if (speed >= 1000000) {
                /* MB/s for very fast drives */
                snprintf(buffer, sizeof(buffer), "%lu.%lu MB/s",
                         (unsigned long)(speed / 1000000),
                         (unsigned long)((speed % 1000000) / 100000));
            } else if (speed >= 10000) {
                /* KB/s for typical drives */
                snprintf(buffer, sizeof(buffer), "%lu KB/s",
                         (unsigned long)(speed / 1000));
            } else {
                /* Bytes/s for very slow devices */
                snprintf(buffer, sizeof(buffer), "%lu B/s", (unsigned long)speed);
            }
        } else {
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_DASH_PLACEHOLDER));
        }
        draw_label_value(120, y, get_string(MSG_SPEED), buffer, 224);
    }

    /* Draw bottom buttons */
    btn = find_button(BTN_DRV_EXIT);
    if (btn) draw_button(btn);
    btn = find_button(BTN_DRV_SCSI);
    if (btn) draw_button(btn);
    btn = find_button(BTN_DRV_SPEED);
    if (btn) draw_button(btn);
}
