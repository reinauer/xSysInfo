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

/* Function prototypes */

/* Export all information to file */
BOOL export_to_file(const char *filename);

/* Individual section exports (used internally) */
void export_header(BPTR fh);
void export_hardware(BPTR fh);
void export_software(BPTR fh);
void export_benchmarks(BPTR fh);
void export_memory(BPTR fh);
void export_boards(BPTR fh);
void export_drives(BPTR fh);

#endif /* PRINT_H */
