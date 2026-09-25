// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#ifndef PROBECLOCK_H
#define PROBECLOCK_H

#include <exec/types.h>
#include <devices/timer.h>

/* Single-owner clock for short hardware probes. Uses ReadEClock on V36+
 * or temporarily reserves a CIA interval timer on older systems.
 * Every successful acquire must be paired with release, even on failure.
 * Call start/read with interrupts disabled. On a CIA, restart for each
 * window and finish within 80 ms; expiry returns zero, never wrapped time.
 * Reads return ticks/second, or zero when no valid timestamp is available.
 */
BOOL acquire_probe_clock(void);
void release_probe_clock(void);
ULONG start_probe_clock(struct EClockVal *value);
ULONG read_probe_clock(struct EClockVal *value);

#endif
