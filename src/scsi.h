// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - SCSI device information header
 */

#ifndef SCSI_H
#define SCSI_H

#include "xsysinfo.h"

/* Maximum SCSI devices we'll track */
#define MAX_SCSI_DEVICES    64

/* Wide SCSI indicator (from Phase V scheme) */
#define HD_WIDESCSI     0x80

/* SCSI device types */
typedef enum {
    SCSI_TYPE_DISK,
    SCSI_TYPE_TAPE,
    SCSI_TYPE_PRINTER,
    SCSI_TYPE_PROCESSOR,
    SCSI_TYPE_WORM,
    SCSI_TYPE_CDROM,
    SCSI_TYPE_SCANNER,
    SCSI_TYPE_OPTICAL,
    SCSI_TYPE_CHANGER,
    SCSI_TYPE_COMM,
    SCSI_TYPE_UNKNOWN
} ScsiDeviceType;

/* SCSI ANSI version */
typedef enum {
    SCSI_ANSI_NONE,
    SCSI_ANSI_SCSI1,
    SCSI_ANSI_SCSI2,
    SCSI_ANSI_SCSI3,
    SCSI_ANSI_UNKNOWN
} ScsiAnsiVersion;

/* SCSI device information */
typedef struct {
    UBYTE target_id;                /* SCSI Target ID (0-15) */
    UBYTE lun;                      /* Logical Unit Number (0-7) */
    ScsiDeviceType device_type;     /* Device type */
    ScsiAnsiVersion ansi_version;   /* ANSI SCSI version */
    char manufacturer[12];          /* Vendor ID (8 chars + null) */
    char model[20];                 /* Product ID (16 chars + null) */
    char revision[8];               /* Product revision (4 chars + null) */
    ULONG max_blocks;               /* Maximum block address */
    ULONG block_size;               /* Block size in bytes */
    ULONG real_size_mb;             /* Real size in MB */
    ULONG format_size_mb;           /* Formatted size in MB */
    BOOL is_valid;                  /* Entry contains valid data */
} ScsiDeviceInfo;

/* SCSI device list */
typedef struct {
    ScsiDeviceInfo devices[MAX_SCSI_DEVICES];
    ULONG count;
    char device_name[64];           /* Device driver name */
} ScsiDeviceList;

/* Global SCSI device list */
extern ScsiDeviceList scsi_device_list;

/* Function prototypes */

/* Check if a device supports SCSI direct commands */
BOOL check_scsi_direct_support(const char *handler_name, ULONG unit_number);

/* Scan all SCSI devices on a controller */
void scan_scsi_devices(const char *handler_name, ULONG base_unit);

/* Draw the SCSI device information screen */
void draw_scsi_view(void);

/* Get SCSI device type string */
const char *get_scsi_type_string(ScsiDeviceType type);

/* Get SCSI ANSI version string */
const char *get_scsi_ansi_string(ScsiAnsiVersion version);

/* Calculate unit number for wide SCSI */
ULONG calculate_unit_number(int target, int lun);

#endif /* SCSI_H */
