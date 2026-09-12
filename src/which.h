// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#ifndef WHICH_H
#define WHICH_H

#include "hardware.h"
#include "software.h"
#include "memory.h"
#include "boards.h"

typedef void (*WhichCompatEmitLine)(void *context, const char *line);

/* Presentation only: inputs must already have been detected/measured. */
void which_compat_emit(const HardwareInfo *hardware,
                       const SystemSoftwareInfo *versions,
                       const MemoryRegionList *memory, const BoardList *boards,
                       WhichCompatEmitLine emit_line, void *context);

BOOL export_which_compatible(BPTR fh);

#endif /* WHICH_H */
