/*
 * xSysInfo - SCSI device enumeration and view
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/scsidisk.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>

/* NSCMD_TD_SCSI is not defined in all NDK versions */
#ifndef NSCMD_TD_SCSI
#define NSCMD_TD_SCSI   0xC004
#endif

#include <proto/exec.h>
#include <proto/graphics.h>

#include "xsysinfo.h"
#include "scsi.h"
#include "gui.h"
#include "locale_str.h"
#include "debug.h"

/* Global SCSI device list */
ScsiDeviceList scsi_device_list;

/* External references */
extern AppContext *app;
extern Button buttons[];
extern int num_buttons;

/* SCSI INQUIRY data structure */
struct SCSIInquiryData {
    UBYTE device_type;          /* Peripheral device type */
    UBYTE device_qualifier;     /* RMB and device type qualifier */
    UBYTE ansi_version;         /* ISO/ECMA/ANSI version */
    UBYTE response_format;      /* Response data format */
    UBYTE additional_length;    /* Additional length */
    UBYTE reserved[3];          /* Reserved */
    char vendor[8];             /* Vendor identification */
    char product[16];           /* Product identification */
    char revision[4];           /* Product revision level */
};

/* SCSI READ CAPACITY data */
struct SCSICapacityData {
    ULONG last_block;           /* Last logical block address */
    ULONG block_size;           /* Block size in bytes */
};

/*
 * Calculate unit number for wide SCSI controllers
 * Phase V wide SCSI scheme for IDs/LUNs > 7
 */
ULONG calculate_unit_number(int target, int lun)
{
    if (target > 7 || lun > 7) {
        /* Phase V wide SCSI scheme for IDs/LUNs > 7 */
        return lun * 10 * 1000 + target * 10 + HD_WIDESCSI;
    } else {
        /* Traditional scheme for IDs/LUNs <= 7 */
        return target + lun * 10;
    }
}

/*
 * Get SCSI device type string
 */
const char *get_scsi_type_string(ScsiDeviceType type)
{
    switch (type) {
        case SCSI_TYPE_DISK:      return "DISK";
        case SCSI_TYPE_TAPE:      return "TAPE";
        case SCSI_TYPE_PRINTER:   return "PRINTER";
        case SCSI_TYPE_PROCESSOR: return "PROCESSOR";
        case SCSI_TYPE_WORM:      return "WORM";
        case SCSI_TYPE_CDROM:     return "CD";
        case SCSI_TYPE_SCANNER:   return "SCANNER";
        case SCSI_TYPE_OPTICAL:   return "OPTICAL";
        case SCSI_TYPE_CHANGER:   return "CHANGER";
        case SCSI_TYPE_COMM:      return "COMM";
        default:                  return "UNKNOWN";
    }
}

/*
 * Get SCSI ANSI version string
 */
const char *get_scsi_ansi_string(ScsiAnsiVersion version)
{
    switch (version) {
        case SCSI_ANSI_NONE:    return "N/A";
        case SCSI_ANSI_SCSI1:   return "SCSI-1";
        case SCSI_ANSI_SCSI2:   return "SCSI-2";
        case SCSI_ANSI_SCSI3:   return "SCSI-3";
        default:               return "?";
    }
}

/*
 * Convert SCSI device type byte to enum
 */
static ScsiDeviceType convert_device_type(UBYTE type)
{
    switch (type & 0x1F) {
        case 0x00: return SCSI_TYPE_DISK;
        case 0x01: return SCSI_TYPE_TAPE;
        case 0x02: return SCSI_TYPE_PRINTER;
        case 0x03: return SCSI_TYPE_PROCESSOR;
        case 0x04: return SCSI_TYPE_WORM;
        case 0x05: return SCSI_TYPE_CDROM;
        case 0x06: return SCSI_TYPE_SCANNER;
        case 0x07: return SCSI_TYPE_OPTICAL;
        case 0x08: return SCSI_TYPE_CHANGER;
        case 0x09: return SCSI_TYPE_COMM;
        default:   return SCSI_TYPE_UNKNOWN;
    }
}

/*
 * Convert SCSI ANSI version byte to enum
 */
static ScsiAnsiVersion convert_ansi_version(UBYTE version)
{
    switch (version & 0x07) {
        case 0: return SCSI_ANSI_NONE;
        case 1: return SCSI_ANSI_SCSI1;
        case 2: return SCSI_ANSI_SCSI2;
        case 3: return SCSI_ANSI_SCSI3;
        default: return SCSI_ANSI_UNKNOWN;
    }
}

/*
 * Check if a device supports SCSI direct commands
 * Returns TRUE if the device responds to HD_SCSICMD or NSCMD_TD_SCSI
 */
BOOL check_scsi_direct_support(const char *handler_name, ULONG unit_number)
{
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    BOOL supports_scsi = FALSE;
    struct SCSICmd scsi_cmd;
    UBYTE cmd[6];

    BYTE error;

    if (!handler_name || !handler_name[0]) {
        return FALSE;
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
    error = OpenDevice((CONST_STRPTR)handler_name, unit_number,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        DeleteIORequest((struct IORequest *)io);
        DeleteMsgPort(port);
        return FALSE;
    }

    memset(&scsi_cmd, 0, sizeof(struct SCSICmd));
    memset(cmd, 0, sizeof(cmd)); // TEST UNIT READY
    
    scsi_cmd.scsi_Command = &cmd;
    scsi_cmd.scsi_Length = 0;
    scsi_cmd.scsi_Flags = SCSIF_READ;

    /* Try HD_SCSICMD first */
    io->io_Command = HD_SCSICMD;
    io->io_Data = &scsi_cmd;
    io->io_Length = sizeof(struct SCSICmd);

    error = DoIO((struct IORequest *)io);

    /* If not IOERR_NOCMD, device supports SCSI commands */
    if (io->io_Error != IOERR_NOCMD) {
        supports_scsi = TRUE;
    } else {
        /* Try NSCMD_TD_SCSI (NewStyle device SCSI command) */
        io->io_Command = NSCMD_TD_SCSI;
        io->io_Data = NULL;
        io->io_Length = 0;

        error = DoIO((struct IORequest *)io);

        if (io->io_Error != IOERR_NOCMD) {
            supports_scsi = TRUE;
        }
    }

    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);

    return supports_scsi;
}

/*
 * Send SCSI INQUIRY command to a device
 */
static BOOL scsi_inquiry(struct IOStdReq *io, int target, int lun,
                         struct SCSIInquiryData *inquiry_data)
{
    struct SCSICmd scsi_cmd;
    UBYTE cmd[6];
    UBYTE sense_data[20];
    BYTE error;
    ULONG unit;

    memset(&scsi_cmd, 0, sizeof(scsi_cmd));
    memset(cmd, 0, sizeof(cmd));
    memset(sense_data, 0, sizeof(sense_data));
    memset(inquiry_data, 0, sizeof(struct SCSIInquiryData));

    /* Calculate unit number for this target/LUN */
    unit = calculate_unit_number(target, lun);

    /* Close and reopen device with correct unit */
    CloseDevice((struct IORequest *)io);
    error = OpenDevice((CONST_STRPTR)scsi_device_list.device_name, unit,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        /* Try opening with unit 0 and addressing via command */
        error = OpenDevice((CONST_STRPTR)scsi_device_list.device_name, 0,
                           (struct IORequest *)io, 0);
        if (error != 0) {
            return FALSE;
        }
    }

    /* SCSI INQUIRY command */
    cmd[0] = 0x12;              /* INQUIRY opcode */
    cmd[1] = (lun << 5);        /* LUN in command for older devices */
    cmd[2] = 0;                 /* Page code */
    cmd[3] = 0;                 /* Reserved */
    cmd[4] = sizeof(struct SCSIInquiryData);  /* Allocation length */
    cmd[5] = 0;                 /* Control */

    scsi_cmd.scsi_Data = (UWORD *)inquiry_data;
    scsi_cmd.scsi_Length = sizeof(struct SCSIInquiryData);
    scsi_cmd.scsi_Command = cmd;
    scsi_cmd.scsi_CmdLength = 6;
    scsi_cmd.scsi_Flags = SCSIF_READ | SCSIF_AUTOSENSE;
    scsi_cmd.scsi_SenseData = sense_data;
    scsi_cmd.scsi_SenseLength = sizeof(sense_data);

    io->io_Command = HD_SCSICMD;
    io->io_Data = &scsi_cmd;
    io->io_Length = sizeof(struct SCSICmd);

    error = DoIO((struct IORequest *)io);

    if (error != 0 || scsi_cmd.scsi_Status != 0) {
        return FALSE;
    }

    return TRUE;
}

/*
 * Send SCSI READ CAPACITY command to a device
 */
static BOOL scsi_read_capacity(struct IOStdReq *io, int target, int lun,
                               struct SCSICapacityData *capacity_data)
{
    struct SCSICmd scsi_cmd;
    UBYTE cmd[10];
    UBYTE sense_data[20];
    UBYTE data[8];
    BYTE error;
    ULONG unit;

    memset(&scsi_cmd, 0, sizeof(scsi_cmd));
    memset(cmd, 0, sizeof(cmd));
    memset(sense_data, 0, sizeof(sense_data));
    memset(data, 0, sizeof(data));

    /* Calculate unit number for this target/LUN */
    unit = calculate_unit_number(target, lun);

    /* Close and reopen device with correct unit */
    CloseDevice((struct IORequest *)io);
    error = OpenDevice((CONST_STRPTR)scsi_device_list.device_name, unit,
                       (struct IORequest *)io, 0);
    if (error != 0) {
        return FALSE;
    }

    /* SCSI READ CAPACITY command */
    cmd[0] = 0x25;              /* READ CAPACITY opcode */
    cmd[1] = (lun << 5);        /* LUN */
    /* Rest are zeros */

    scsi_cmd.scsi_Data = (UWORD *)data;
    scsi_cmd.scsi_Length = 8;
    scsi_cmd.scsi_Command = cmd;
    scsi_cmd.scsi_CmdLength = 10;
    scsi_cmd.scsi_Flags = SCSIF_READ | SCSIF_AUTOSENSE;
    scsi_cmd.scsi_SenseData = sense_data;
    scsi_cmd.scsi_SenseLength = sizeof(sense_data);

    io->io_Command = HD_SCSICMD;
    io->io_Data = &scsi_cmd;
    io->io_Length = sizeof(struct SCSICmd);

    error = DoIO((struct IORequest *)io);

    if (error != 0 || scsi_cmd.scsi_Status != 0) {
        return FALSE;
    }

    /* Data is big-endian, convert to host order */
    capacity_data->last_block = (data[0] << 24) | (data[1] << 16) |
                                (data[2] << 8) | data[3];
    capacity_data->block_size = (data[4] << 24) | (data[5] << 16) |
                                (data[6] << 8) | data[7];

    return TRUE;
}

/*
 * Trim trailing spaces from a string
 */
static void trim_trailing_spaces(char *str)
{
    int len = strlen(str);
    while (len > 0 && str[len - 1] == ' ') {
        str[--len] = '\0';
    }
}

/*
 * Scan all SCSI devices on a controller
 */
void scan_scsi_devices(const char *handler_name, ULONG base_unit)
{
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    struct SCSIInquiryData inquiry_data;
    struct SCSICapacityData capacity_data;
    int target, lun;
    BYTE error;

    (void)base_unit;  /* Not used in current implementation */

    memset(&scsi_device_list, 0, sizeof(scsi_device_list));
    strncpy(scsi_device_list.device_name, handler_name,
            sizeof(scsi_device_list.device_name) - 1);

    debug("  scsi: Scanning SCSI devices on %s\n", (LONG)handler_name);

    /* Create message port */
    port = CreateMsgPort();
    if (!port) {
        debug("  scsi: Failed to create message port\n");
        return;
    }

    /* Create I/O request */
    io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
    if (!io) {
        debug("  scsi: Failed to create IO request\n");
        DeleteMsgPort(port);
        return;
    }

    /* Scan all possible SCSI IDs (0-15 for wide SCSI) */
    for (target = 0; target < 16; target++) {
        /* For now just check LUN 0 */
        for (lun = 0; lun < 1; lun++) {
            ULONG unit = calculate_unit_number(target, lun);

            /* Open device with this unit */
            error = OpenDevice((CONST_STRPTR)handler_name, unit,
                               (struct IORequest *)io, 0);
            if (error != 0) {
                continue;
            }

            /* Try INQUIRY command */
            if (scsi_inquiry(io, target, lun, &inquiry_data)) {
                /* Check if device is present (not 0x7F = no device) */
                if ((inquiry_data.device_type & 0x1F) != 0x1F) {
                    ScsiDeviceInfo *dev = &scsi_device_list.devices[scsi_device_list.count];

                    dev->target_id = target;
                    dev->lun = lun;
                    dev->device_type = convert_device_type(inquiry_data.device_type);
                    dev->ansi_version = convert_ansi_version(inquiry_data.ansi_version);

                    /* Copy and trim strings */
                    memcpy(dev->manufacturer, inquiry_data.vendor, 8);
                    dev->manufacturer[8] = '\0';
                    trim_trailing_spaces(dev->manufacturer);

                    memcpy(dev->model, inquiry_data.product, 16);
                    dev->model[16] = '\0';
                    trim_trailing_spaces(dev->model);

                    memcpy(dev->revision, inquiry_data.revision, 4);
                    dev->revision[4] = '\0';
                    trim_trailing_spaces(dev->revision);

                    /* Try to get capacity info */
                    if (scsi_read_capacity(io, target, lun, &capacity_data)) {
                        dev->max_blocks = capacity_data.last_block;
                        dev->block_size = capacity_data.block_size;

                        if (dev->block_size > 0) {
                            /* Calculate sizes in MB */
                            ULONG total_blocks = capacity_data.last_block + 1;
                            ULONG size_kb = (total_blocks / 1024) * dev->block_size +
                                            ((total_blocks % 1024) * dev->block_size) / 1024;
                            dev->real_size_mb = size_kb / 1024;
                            dev->format_size_mb = dev->real_size_mb;  /* Simplified */
                        }
                    } else {
                        dev->max_blocks = 0;
                        dev->block_size = 0;
                        dev->real_size_mb = 0;
                        dev->format_size_mb = 0;
                    }

                    dev->is_valid = TRUE;
                    scsi_device_list.count++;

                    debug("  scsi: Found device ID %ld: %s %s\n",
                          (LONG)target, (LONG)dev->manufacturer, (LONG)dev->model);

                    if (scsi_device_list.count >= MAX_SCSI_DEVICES) {
                        break;
                    }
                }
            }

            CloseDevice((struct IORequest *)io);
        }

        if (scsi_device_list.count >= MAX_SCSI_DEVICES) {
            break;
        }
    }

    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);

    debug("  scsi: Scan complete, found %ld devices\n", (LONG)scsi_device_list.count);
}

/*
 * Format size as MB string, or "?" if unknown
 */
static void format_size_mb(ULONG size_mb, char *buffer, ULONG bufsize)
{
    if (size_mb > 0) {
        snprintf(buffer, bufsize, "%luMB", (unsigned long)size_mb);
    } else {
        snprintf(buffer, bufsize, "?");
    }
}

/*
 * Draw the SCSI device information screen
 */
void draw_scsi_view(void)
{
    struct RastPort *rp = app->rp;
    char buffer[128];
    WORD y;
    ULONG i;
    Button *btn;

    /* Clear background */
    SetAPen(rp, COLOR_BACKGROUND);
    RectFill(rp, 0, 0, SCREEN_WIDTH - 1, app->screen_height - 1);

    /* Draw title panel */
    draw_panel(20, 0, 600, 24, NULL);

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 250, 14);
    Text(rp, (CONST_STRPTR)get_string(MSG_SCSI_INFO), strlen(get_string(MSG_SCSI_INFO)));

    /* Draw column headers */
    draw_panel(20, 28, 600, 16, NULL);

    y = 40;
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);

    Move(rp, 28, y);
    Text(rp, (CONST_STRPTR)"ID", 2);
    Move(rp, 56, y);
    Text(rp, (CONST_STRPTR)"Type", 4);
    Move(rp, 112, y);
    Text(rp, (CONST_STRPTR)"Manuf", 5);
    Move(rp, 200, y);
    Text(rp, (CONST_STRPTR)"Model", 5);
    Move(rp, 328, y);
    Text(rp, (CONST_STRPTR)"Ver", 3);
    Move(rp, 368, y);
    Text(rp, (CONST_STRPTR)"MaxBlocks", 9);
    Move(rp, 448, y);
    Text(rp, (CONST_STRPTR)"ANSI", 4);
    Move(rp, 504, y);
    Text(rp, (CONST_STRPTR)"Real", 4);
    Move(rp, 560, y);
    Text(rp, (CONST_STRPTR)"Format", 6);

    /* Draw device list panel */
    draw_panel(20, 46, 600, 130, NULL);

    /* Draw devices */
    y = 60;
    for (i = 0; i < scsi_device_list.count && i < 12; i++) {
        ScsiDeviceInfo *dev = &scsi_device_list.devices[i];

        if (!dev->is_valid) continue;

        SetAPen(rp, COLOR_HIGHLIGHT);
        SetBPen(rp, COLOR_PANEL_BG);

        /* ID */
        snprintf(buffer, sizeof(buffer), "%d", dev->target_id);
        Move(rp, 28, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* Type */
        Move(rp, 56, y);
        Text(rp, (CONST_STRPTR)get_scsi_type_string(dev->device_type),
             strlen(get_scsi_type_string(dev->device_type)));

        /* Manufacturer */
        Move(rp, 112, y);
        Text(rp, (CONST_STRPTR)dev->manufacturer, strlen(dev->manufacturer));

        /* Model */
        Move(rp, 200, y);
        Text(rp, (CONST_STRPTR)dev->model, strlen(dev->model));

        /* Revision */
        Move(rp, 328, y);
        Text(rp, (CONST_STRPTR)dev->revision, strlen(dev->revision));

        /* MaxBlocks */
        if (dev->max_blocks > 0) {
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)dev->max_blocks);
        } else {
            snprintf(buffer, sizeof(buffer), "0");
        }
        Move(rp, 368, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* ANSI version */
        Move(rp, 448, y);
        Text(rp, (CONST_STRPTR)get_scsi_ansi_string(dev->ansi_version),
             strlen(get_scsi_ansi_string(dev->ansi_version)));

        /* Real size */
        format_size_mb(dev->real_size_mb, buffer, sizeof(buffer));
        Move(rp, 504, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* Format size */
        format_size_mb(dev->format_size_mb, buffer, sizeof(buffer));
        Move(rp, 560, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        y += 10;
    }

    if (scsi_device_list.count == 0) {
        SetAPen(rp, COLOR_TEXT);
        Move(rp, 250, 100);
        Text(rp, (CONST_STRPTR)"No SCSI devices found", 21);
    }

    /* Draw EXIT button */
    btn = find_button(BTN_SCSI_EXIT);
    if (btn) draw_button(btn);
}
