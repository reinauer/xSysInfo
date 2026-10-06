// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors
#ifndef FORMAT_H
#define FORMAT_H

#include "xsysinfo.h"
#include "locale_str.h"
#include "cache.h"
#include "boards.h"

#define MAX_HARDWARE_INFO_ROWS 64
#define SOFTWARE_OVERVIEW_MAX_ROWS 7

/* Values are valid only during the visitor call. Groups keep related rows
 * together; detail marks subordinate fields. Frontends choose the layout. */
typedef struct {
    const char *label;
    const char *value; /* NULL for headings and cache controls */
    UWORD group;
    BOOL detail;
    CacheSetting control;
} HardwareInfoRow;

typedef void (*HardwareRowVisitor)(const HardwareInfoRow *row, void *data);
void visit_hardware_rows(HardwareType page, HardwareRowVisitor visit,
                         void *data);
void format_clock_values(char values[2][24]);
ULONG software_overview_count(void);
LocaleStringID software_overview_label(ULONG row);
const char *format_software_overview_value(ULONG row, char *buffer, size_t size);
ULONG speed_scale_max(BarScale scale);
ULONG scale_speed_value(ULONG value, ULONG max_value, ULONG extent,
                        BarScale scale);
void format_transfer_rate(ULONG speed, BOOL fractional_kb,
                          char *buffer, size_t size);
/* Board fields share formatting between both lists and detail views. */
typedef enum {
    BOARD_FIELD_ADDRESS, BOARD_FIELD_SIZE, BOARD_FIELD_TYPE,
    BOARD_FIELD_PRODUCT, BOARD_FIELD_MANUFACTURER, BOARD_FIELD_SERIAL,
    BOARD_FIELD_SYSTEM_MEMORY, BOARD_FIELD_MEMORY_SPACE,
    BOARD_FIELD_ROM_VALID, BOARD_FIELD_ROM_VECTOR, BOARD_FIELD_CHAINED,
    BOARD_FIELD_SHUTUP, BOARD_FIELD_ZORRO_III, BOARD_FIELD_EXTENDED,
    BOARD_FIELD_SUBSIZE, BOARD_FIELD_COUNT
} BoardField;

ULONG board_detail_count(const BoardInfo *board);
LocaleStringID board_field_label(BoardField field, BoardType type);
const char *format_board_field(const BoardInfo *board, BoardDisplay display,
                              BoardField field, char *buffer, size_t size);
#endif
