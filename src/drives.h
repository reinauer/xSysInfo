// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Drives information header
 */

#ifndef DRIVES_H
#define DRIVES_H

#include "xsysinfo.h"

/* Maximum drives we'll track */
#define MAX_DRIVES  32

/* Disk state */
typedef enum {
    DISK_OK,
    DISK_WRITE_PROTECTED,
    DISK_NO_DISK,
    DISK_UNREADABLE,
    DISK_UNKNOWN
} DiskState;

/* Filesystem types */
typedef enum {
    FS_OFS,
    FS_FFS,
    FS_INTL_OFS,
    FS_INTL_FFS,
    FS_DCACHE_OFS,
    FS_DCACHE_FFS,
    FS_SFS,
    FS_PFS,
    FS_UNKNOWN
} FilesystemType;

/* Drive information */
typedef struct {
    char device_name[32];       /* e.g., "DF0:" */
    char volume_name[64];       /* Volume label */
    char handler_name[64];      /* e.g., "trackdisk.device" */
    ULONG unit_number;
    DiskState disk_state;
    ULONG total_blocks;
    ULONG blocks_used;
    ULONG bytes_per_block;
    FilesystemType fs_type;
    ULONG dos_type;             /* Raw DOS type */
    ULONG surfaces;
    ULONG sectors_per_track;
    ULONG reserved_blocks;
    ULONG low_cylinder;
    ULONG high_cylinder;
    ULONG num_buffers;
    ULONG speed_bytes_sec;      /* 0 = not measured */
    ULONG disk_errors;
    BOOL speed_measured;
    BOOL scsi_supported;        /* TRUE if device supports SCSI direct commands */
    BOOL is_valid;              /* Entry contains valid data */
} DriveInfo;

/* Drive list */
typedef struct {
    DriveInfo drives[MAX_DRIVES];
    ULONG count;
} DriveList;

/* Global drive list */
extern DriveList drive_list;

/* Function prototypes */
void enumerate_drives(void);
void refresh_drive_info(ULONG index);
ULONG measure_drive_speed(ULONG index);
BOOL check_disk_present(ULONG index);
ULONG get_display_block_size(const DriveInfo *drive);

/* Helper functions */
const char *get_disk_state_string(DiskState state);
const char *get_filesystem_string(FilesystemType fs);
FilesystemType identify_filesystem(ULONG dos_type);

#endif /* DRIVES_H */
