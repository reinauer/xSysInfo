// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#ifndef BUSCLOCK_H
#define BUSCLOCK_H

/* Set hw_info.bus_mhz to the measured clock in MHz * 100, or zero if unavailable. */
void measure_bus_frequency(void);

#endif
