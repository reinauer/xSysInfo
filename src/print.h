// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Print/export functions header
 */

#ifndef PRINT_H
#define PRINT_H

#include "xsysinfo.h"

/* Default output file path */
#define DEFAULT_OUTPUT_FILE "RAM:xsysinfo.txt"
#define MAX_FILENAME_LEN 128

typedef enum {
    REPORT_WHICH, REPORT_BRIEF, REPORT_FULL, REPORT_COUNT
} ReportFormat;

typedef struct {
    char *text;             /* Owned storage; lines point into this buffer. */
    char **lines;
    ULONG count, width;     /* Line count and longest line in characters. */
    ReportFormat format;
} ReportText;

/* Streaming CLI output and previews share the same report generators. */
BOOL export_report_to_handle(BPTR fh, ReportFormat format);
/* Destination must be empty; it remains untouched on failure. */
BOOL create_report(ReportText *report, ReportFormat format);
void free_report(ReportText *report);
BOOL save_report(const ReportText *report, const char *filename);
const char *report_format_name(ReportFormat format);

#endif /* PRINT_H */
