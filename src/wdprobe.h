// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#ifndef WDPROBE_H
#define WDPROBE_H

#include "xsysinfo.h"

typedef enum { WD_UNKNOWN, WD_33C93, WD_33C93A, WD_33C93B } WDChip;
typedef enum {
    WD_PROBE_OK, WD_PROBE_UNAVAILABLE, WD_PROBE_BUSY,
    WD_PROBE_FAILED, WD_PROBE_CLOCK_FAILED, WD_PROBE_RESTORE_FAILED,
    WD_PROBE_NOT_APPLICABLE
} WDProbeStatus;

typedef struct {
    WDChip chip;
    UBYTE microcode, control, timeout, sync;
    ULONG clock_khz;
} WDProbeInfo;

enum { WD_DETAIL_MICROCODE, WD_DETAIL_CLOCK, WD_DETAIL_MODE,
       WD_DETAIL_TIMEOUT, WD_DETAIL_SYNC, WD_DETAIL_COUNT };

extern WDProbeInfo wd_info;

/* Resets the controller: call only when SCSI is enabled. */
WDProbeStatus probe_wd_controller(void);
void format_wd_detail(unsigned detail, char *buffer, ULONG size);

#endif
