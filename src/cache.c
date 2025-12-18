// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - CPU cache control
 */

#include <proto/exec.h>

#include "xsysinfo.h"
#include "cache.h"
#include "hardware.h"

/* External references */
extern HardwareInfo hw_info;

/*
 * Helper to toggle a cache flag
 */
static void toggle_cache_flag(ULONG flag)
{
    ULONG current = CacheControl(0, 0);

    if (current & flag) {
        /* Disable */
        CacheControl(0, flag);
    } else {
        /* Enable */
        CacheControl(flag, flag);
    }
}

/*
 * Toggle instruction cache
 */
void toggle_icache(void)
{
    if (cpu_has_icache()) {
        toggle_cache_flag(CACRF_EnableI);
    }
}

/*
 * Toggle data cache
 */
void toggle_dcache(void)
{
    if (cpu_has_dcache()) {
        toggle_cache_flag(CACRF_EnableD);
    }
}

/*
 * Toggle instruction burst
 */
void toggle_iburst(void)
{
    if (cpu_has_iburst()) {
        toggle_cache_flag(CACRF_IBE);
    }
}

/*
 * Toggle data burst
 */
void toggle_dburst(void)
{
    if (cpu_has_dburst()) {
        toggle_cache_flag(CACRF_DBE);
    }
}

/*
 * Toggle copyback mode
 */
void toggle_copyback(void)
{
    if (cpu_has_copyback()) {
        toggle_cache_flag(CACRF_CopyBack);
    }
}

/*
 * Check if CPU has instruction cache
 */
BOOL cpu_has_icache(void)
{
    return hw_info.has_icache;
}

/*
 * Check if CPU has data cache
 */
BOOL cpu_has_dcache(void)
{
    return hw_info.has_dcache;
}

/*
 * Check if CPU has instruction burst mode
 */
BOOL cpu_has_iburst(void)
{
    return hw_info.has_iburst;
}

/*
 * Check if CPU has data burst mode
 */
BOOL cpu_has_dburst(void)
{
    return hw_info.has_dburst;
}

/*
 * Check if CPU has copyback mode
 */
BOOL cpu_has_copyback(void)
{
    return hw_info.has_copyback;
}
