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

        /* Open dos.library. */

        if ((DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37))) { //check for right version!
            CloseLibrary ((struct Library *) DOSBase); // Availability check over we can close this now!

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
        result &= WriteBattMem (&Data, BATTMEM_SCSI_TIMEOUT_ADDR, BATTMEM_SCSI_TIMEOUT_LEN);

        /* Scan LUNs? */
        Data = src->scan_luns ? 1 : 0;
        result &= WriteBattMem (&Data, BATTMEM_SCSI_LUNS_ADDR, BATTMEM_SCSI_LUNS_LEN);

        /* Synchronous transfer enabled? */
        Data = src->sync_transfer ? 1 : 0;
        result &= WriteBattMem (&Data, BATTMEM_SCSI_SYNC_XFER_ADDR, BATTMEM_SCSI_SYNC_XFER_LEN);

        /* Fast synchronous transfer enabled? */
        Data = src->fast_sync_transfer ? 1 : 0;
        result &= WriteBattMem (&Data, BATTMEM_SCSI_FAST_SYNC_ADDR, BATTMEM_SCSI_FAST_SYNC_LEN);


        /* SCSI-2 tagged queuing enabled? */
        Data = src->tagged_queuing ? 1 : 0;
        result &= WriteBattMem (&Data, BATTMEM_SCSI_TAG_QUEUES_ADDR, BATTMEM_SCSI_TAG_QUEUES_LEN);

        /* Show SCSI host ID. */
        if (src->scsi_id < 8) {
            Data = src->scsi_id ^ 7;
            WriteBattMem (&Data, BATTMEM_SCSI_HOST_ID_ADDR, BATTMEM_SCSI_HOST_ID_LEN);
        }
        else {
            /*What should we doo if a wrong ID is provided?*/
        }

        /* If write was successfull: reset amnesia on the both sides */
        if (result) {
            Data = 1;
            WriteBattMem (&Data, BATTMEM_AMIGA_AMNESIA_ADDR, BATTMEM_AMIGA_AMNESIA_LEN);
            Data = 1;
            WriteBattMem (&Data, BATTMEM_SHARED_AMNESIA_ADDR, BATTMEM_SHARED_AMNESIA_LEN);
        }

        ReleaseBattSemaphore ();
    }
    return result;
}
