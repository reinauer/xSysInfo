// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Matthias Heinrichs

/*
 * xSysInfo - Battmem functions
 */


#include "battmem.h"
#include <resources/battmembitsshared.h>
#include <resources/battmembitsamiga.h>

#include <resources/battmem.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <exec/memory.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/battmem.h>

// Shared extern DOSlibrary
extern struct DosLibrary *DOSBase;

//global BattMemBase
struct Library *BattMemBase = NULL;

BOOL openBattMem(void) //returns false, if not available
{
    if (BattMemBase == NULL) {

        if (DOSBase && DOSBase->dl_lib.lib_Version >= 37) {
            /* Open battmem.resource. */
            BattMemBase = (struct Library *) OpenResource ((CONST_STRPTR)BATTMEMNAME);
        }
    }
    return BattMemBase != NULL;
}


BOOL readBattMem(BattMemData* dest)//returns false on error
{
    UBYTE Data;
    BOOL result = openBattMem();
    if (result) {
        ObtainBattSemaphore ();

        /* Amnesia on the Amiga side? */
        if (!ReadBattMem (&Data, BATTMEM_AMIGA_AMNESIA_ADDR, BATTMEM_AMIGA_AMNESIA_LEN)) {
            dest->amnesia_amiga = !(Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Amnesia on the shared Amiga/Amix side? */
        if (!ReadBattMem (&Data, BATTMEM_SHARED_AMNESIA_ADDR, BATTMEM_SHARED_AMNESIA_LEN)) {
            dest->amnesia_shared = !(Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Long or short timeouts? */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_TIMEOUT_ADDR, BATTMEM_SCSI_TIMEOUT_LEN)) {
            dest->long_timeout = (Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Scan LUNs? */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_LUNS_ADDR, BATTMEM_SCSI_LUNS_LEN)) {
            dest->scan_luns = (Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Synchronous transfer enabled? */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_SYNC_XFER_ADDR, BATTMEM_SCSI_SYNC_XFER_LEN)) {
            dest->sync_transfer = (Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Fast synchronous transfer enabled? */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_FAST_SYNC_ADDR, BATTMEM_SCSI_FAST_SYNC_LEN)) {
            dest->fast_sync_transfer = (Data & 1);
        }
        else {
            result = FALSE;
        }


        /* SCSI-2 tagged queuing enabled? */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_TAG_QUEUES_ADDR, BATTMEM_SCSI_TAG_QUEUES_LEN)) {
            dest->tagged_queuing = (Data & 1);
        }
        else {
            result = FALSE;
        }

        /* Show SCSI host ID. */
        if (!ReadBattMem (&Data, BATTMEM_SCSI_HOST_ID_ADDR, BATTMEM_SCSI_HOST_ID_LEN)) {
            dest->scsi_id = (Data & 7) ^ 7;
        }
        else {
            result = FALSE;
        }

        ReleaseBattSemaphore ();
    }

    return result;
}
BOOL writeBattMem(BattMemData* src) //returns false on error
{
    UBYTE Data;
    BOOL result = openBattMem();

    if (result) {
        ObtainBattSemaphore ();

        /* Long or short timeouts? */
        Data = src->long_timeout ? 1 : 0;
        if (WriteBattMem (&Data, BATTMEM_SCSI_TIMEOUT_ADDR, BATTMEM_SCSI_TIMEOUT_LEN)) {
            result = FALSE;
        }

        /* Scan LUNs? */
        Data = src->scan_luns ? 1 : 0;
        if (WriteBattMem (&Data, BATTMEM_SCSI_LUNS_ADDR, BATTMEM_SCSI_LUNS_LEN)) {
            result = FALSE;
        }

        /* Synchronous transfer enabled? */
        Data = src->sync_transfer ? 1 : 0;
        if (WriteBattMem (&Data, BATTMEM_SCSI_SYNC_XFER_ADDR, BATTMEM_SCSI_SYNC_XFER_LEN)) {
            result = FALSE;
        }

        /* Fast synchronous transfer enabled? */
        Data = src->fast_sync_transfer ? 1 : 0;
        if (WriteBattMem (&Data, BATTMEM_SCSI_FAST_SYNC_ADDR, BATTMEM_SCSI_FAST_SYNC_LEN)) {
            result = FALSE;
        }


        /* SCSI-2 tagged queuing enabled? */
        Data = src->tagged_queuing ? 1 : 0;
        if (WriteBattMem (&Data, BATTMEM_SCSI_TAG_QUEUES_ADDR, BATTMEM_SCSI_TAG_QUEUES_LEN)) {
            result = FALSE;
        }

        /* Show SCSI host ID. */
        if (src->scsi_id < 8) {
            Data = src->scsi_id ^ 7;
            if (WriteBattMem (&Data, BATTMEM_SCSI_HOST_ID_ADDR, BATTMEM_SCSI_HOST_ID_LEN)) {
                result = FALSE;
            }
        }
        else {
            /*What should we do if a wrong ID is provided?*/
        }

        /* If write was successful: reset amnesia on the both sides */
        if (result) {
            Data = 1;
            if (WriteBattMem (&Data, BATTMEM_AMIGA_AMNESIA_ADDR, BATTMEM_AMIGA_AMNESIA_LEN)) {
                result = FALSE;
            }
            Data = 1;
            if (WriteBattMem (&Data, BATTMEM_SHARED_AMNESIA_ADDR, BATTMEM_SHARED_AMNESIA_LEN)) {
                result = FALSE;
            }
        }

        ReleaseBattSemaphore ();
    }
    return result;
}
