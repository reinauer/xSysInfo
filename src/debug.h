// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Debug output support
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <proto/dos.h>
#include <stdarg.h>
#include <stdio.h>

/* Global debug flag - set via /D command line switch */
extern BOOL g_debug_enabled;

/* Use our Kickstart 1.3-compatible C runtime: DOS VPrintf needs V36.
 * Keep diagnostics unbuffered so the last line survives a failed probe.
 */
static inline void debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

#define debug(fmt, ...) \
    do { \
        if (g_debug_enabled) \
            debug_printf((const char *)(fmt), ##__VA_ARGS__); \
    } while (0)

/* These error messages must also work before the display is opened. */
#undef Printf
#define Printf(fmt, ...) debug_printf((const char *)(fmt), ##__VA_ARGS__)

#endif /* DEBUG_H */
