// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Expansion boards information header
 */

#ifndef BOARDS_H
#define BOARDS_H

#include "xsysinfo.h"

/* Maximum boards we'll track */
#define MAX_BOARDS  32

/* Board type */
typedef enum {
    BOARD_ZORRO_II,
    BOARD_ZORRO_III,
    BOARD_UNKNOWN
} BoardType;

/* Board information */
typedef struct {
    ULONG board_address;
    ULONG board_size;
    UWORD manufacturer_id;
    UBYTE product_id;
    BoardType board_type;
    LONG serial_number;
    char product_name[64];
    char manufacturer_name[64];
    char size_string[16];       /* Human-readable size */
} BoardInfo;

/* Board list */
typedef struct {
    BoardInfo boards[MAX_BOARDS];
    ULONG count;
} BoardList;

/* Global board list */
extern BoardList board_list;

/* Function prototypes */
void enumerate_boards(void);

/* Helper functions */
const char *get_board_type_string(BoardType type);
void format_board_size(ULONG size, char *buffer, ULONG bufsize);

#endif /* BOARDS_H */
