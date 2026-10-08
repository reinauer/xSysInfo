// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/*
 * xSysInfo - Lists sized to their contents instead of fixed maximums
 */

#include <string.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "growlist.h"

#define GROW_STEP 16

BOOL grow_list(APTR *items, ULONG *capacity, ULONG count, ULONG needed,
               ULONG item_size, ULONG max)
{
    ULONG size;
    APTR grown;

    if (needed <= *capacity)
        return TRUE;
    if (needed > max)
        return FALSE;
    size = (needed + GROW_STEP - 1) / GROW_STEP * GROW_STEP;
    if (size > max)
        size = max;
    /* AllocMem() does not break Forbid(), so scans may grow lists. */
    grown = AllocMem(size * item_size, MEMF_ANY | MEMF_CLEAR);
    if (!grown)
        return FALSE;
    if (*items) {
        memcpy(grown, *items, count * item_size);
        FreeMem(*items, *capacity * item_size);
    }
    *items = grown;
    *capacity = size;
    return TRUE;
}

void free_list(APTR *items, ULONG *capacity, ULONG item_size)
{
    if (*items)
        FreeMem(*items, *capacity * item_size);
    *items = NULL;
    *capacity = 0;
}
