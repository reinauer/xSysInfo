// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#ifndef XSYSINFO_CLOCK_H
#define XSYSINFO_CLOCK_H

#include <exec/types.h>
#include <utility/date.h>

BOOL read_hardware_time(struct ClockData *date);

/* A separate GUI timer, armed only while the Clock page is visible. */
ULONG clock_refresh_signal(BOOL enabled);
BOOL clock_refresh_ready(void);
void cleanup_clock_refresh(void);

#endif
