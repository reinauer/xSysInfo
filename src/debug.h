// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Debug output support
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <proto/dos.h>

/* Set by the DEBUG command line option or Workbench tooltype. */
extern BOOL g_debug_enabled;

BOOL init_debug_output(BOOL workbench);
void cleanup_debug_output(void);
void debug_printf(const char *fmt, ...);

#define debug(fmt, ...) \
    do { \
        if (g_debug_enabled) \
            debug_printf((const char *)(fmt), ##__VA_ARGS__); \
    } while (0)

/* These error messages must also work before the display is opened. */
#undef Printf
#define Printf(fmt, ...) debug_printf((const char *)(fmt), ##__VA_ARGS__)

#endif /* DEBUG_H */
