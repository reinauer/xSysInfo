// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"

static BPTR debug_output;
static BPTR debug_file;

BOOL init_debug_output(BOOL workbench)
{
    if (!workbench) {
        debug_output = Output();
    } else if (g_debug_enabled) {
        debug_file = Open((CONST_STRPTR)"RAM:xSysInfo.log", MODE_NEWFILE);
        debug_output = debug_file;
        if (!debug_file)
            return FALSE;
    }

    return TRUE;
}

void cleanup_debug_output(void)
{
    debug_output = 0;
    if (debug_file) {
        Close(debug_file);
        debug_file = 0;
    }
}

/* Format with the Kickstart 1.3-compatible runtime, then write directly.
 * Buffered stdio can overrun its buffer after an output error in libnix.
 */
void debug_printf(const char *fmt, ...)
{
    char buffer[256];
    char *text = buffer;
    va_list args;
    int length;
    LONG offset = 0;

    if (!debug_output)
        return;

    va_start(args, fmt);
    length = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (length < 0)
        return;

    /* Keep ordinary messages on the stack without truncating longer ones. */
    if ((unsigned int)length >= sizeof(buffer)) {
        text = malloc((size_t)length + 1);
        if (!text)
            return;
        va_start(args, fmt);
        vsnprintf(text, (size_t)length + 1, fmt, args);
        va_end(args);
    }

    while (offset < length) {
        LONG written = Write(debug_output, text + offset, length - offset);

        if (written <= 0) {
            debug_output = 0;
            break;
        }
        offset += written;
    }

    if (text != buffer)
        free(text);
}
