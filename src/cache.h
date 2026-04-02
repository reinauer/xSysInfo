// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - CPU cache control header
 */

#ifndef CACHE_H
#define CACHE_H

#include "xsysinfo.h"

/* 68040/060 CACR bit definitions not in the standard headers */
#define CACRF_ICACHE040	0x8000
#define CACRF_EBC060	(1 << 29)
#define CACRF_ESB060	(1 << 23)

/* CACR bit format conversion between 68030 and 68040/060 */
ULONG convert68030to68040(ULONG input);
ULONG convert68040to68030(ULONG input);
ULONG convertFlagsFor68040(ULONG input);

/* Toggle cache settings */
void toggle_icache(void);
void toggle_dcache(void);
void toggle_iburst(void);
void toggle_dburst(void);
void toggle_copyback(void);
void toggle_super_scalar(void);

/* Read current cache state */
void read_cache_state(BOOL *icache, BOOL *dcache,
                      BOOL *iburst, BOOL *dburst, BOOL *copyback);

/* Check what cache features are available on this CPU */
BOOL cpu_has_icache(void);
BOOL cpu_has_dcache(void);
BOOL cpu_has_iburst(void);
BOOL cpu_has_dburst(void);
BOOL cpu_has_copyback(void);
BOOL cpu_has_super_scalar(void);

#endif /* CACHE_H */
