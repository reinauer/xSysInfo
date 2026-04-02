// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Debug output support
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <proto/dos.h>

/* Global debug flag - set via /D command line switch */
extern BOOL g_debug_enabled;

/* Debug output macro - only prints if debugging is enabled */

#ifdef __KICK13__
    #define debug(fmt, ...) \
        do { \
            if (g_debug_enabled) { \
                printf((const char *)fmt, ##__VA_ARGS__); \
            } \
        } while (0)

    #define Printf(fmt, ...) \
        do { \
            printf((const char *)fmt, ##__VA_ARGS__); \
        } while (0)
#else
    #define debug(fmt, ...) \
        do { \
            if (g_debug_enabled) { \
                Printf((CONST_STRPTR)fmt, ##__VA_ARGS__); \
            } \
        } while (0)
#endif /*__KICK13__*/

#endif /* DEBUG_H */
