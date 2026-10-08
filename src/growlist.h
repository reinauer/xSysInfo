// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/*
 * xSysInfo - Lists sized to their contents instead of fixed maximums
 */

#ifndef GROWLIST_H
#define GROWLIST_H

#include <exec/types.h>

/* Make room for at least needed items, growing in steps so a typical
 * list needs one allocation. New items are zeroed. Returns FALSE at
 * max items or when memory runs out; the list keeps its contents. */
BOOL grow_list(APTR *items, ULONG *capacity, ULONG count, ULONG needed,
               ULONG item_size, ULONG max);
void free_list(APTR *items, ULONG *capacity, ULONG item_size);

#endif /* GROWLIST_H */
