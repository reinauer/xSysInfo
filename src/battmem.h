// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Matthias Heinrichs

/*
 * xSysInfo - Battmem header
 */

#ifndef BATTMEM_H
#define BATTMEM_H

#include "xsysinfo.h"


/* Reference system data */
typedef struct {
    BOOL available;
    BOOL valid_data;
    BOOL amnesia_amiga;
    BOOL amnesia_shared;
    BOOL long_timeout;
    BOOL scan_luns;
    BOOL sync_transfer;
    BOOL fast_sync_transfer;
    BOOL tagged_queuing;
    unsigned char scsi_id;
} BattMemData;

/* Access functions */
BOOL openBattMem(void); //returns false, if not available
BOOL readBattMem(BattMemData* dest); //returns false on error
BOOL writeBattMem(BattMemData* src); //returns false on error

#endif /* BATTMEM_H */
