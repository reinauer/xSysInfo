// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Expansion boards (Zorro) enumeration and view
 */

#include <string.h>
#include <stdio.h>

#include <libraries/configvars.h>
#include <libraries/identify.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <proto/graphics.h>
#include <proto/identify.h>

#include "xsysinfo.h"
#include "boards.h"
#include "gui.h"
#include "locale_str.h"
#include "debug.h"
#ifdef __KICK13__
#include "tagitem.h"
#endif

/* Global board list */
BoardList board_list;

/* External references */
extern AppContext *app;
extern struct Library *IdentifyBase;

/*
 * Format board size to human-readable string
 */
void format_board_size(ULONG size, char *buffer, ULONG bufsize)
{
    if (size >= 1024 * 1024) {
        snprintf(buffer, bufsize, "%luM", (unsigned long)(size / (1024 * 1024)));
    } else if (size >= 1024) {
        snprintf(buffer, bufsize, "%luK", (unsigned long)(size / 1024));
    } else {
        snprintf(buffer, bufsize, "%lu", (unsigned long)size);
    }
}

/*
 * Get board type string
 */
const char *get_board_type_string(BoardType type)
{
    switch (type) {
        case BOARD_ZORRO_II:
            return get_string(MSG_ZORRO_II);
        case BOARD_ZORRO_III:
            return get_string(MSG_ZORRO_III);
        default:
            return get_string(MSG_UNKNOWN);
    }
}

/*
 * Enumerate all expansion boards
 */
void enumerate_boards(void)
{
    struct ConfigDev *cd = NULL;
    struct Library *ExpansionBase;

    debug("  boards: Starting enumeration...\n");

    memset(&board_list, 0, sizeof(board_list));

    debug("  boards: Opening expansion.library...\n");
    ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", MIN_EXPANSION_VERSION);
    if (!ExpansionBase) {
        Printf((CONST_STRPTR)"Could not open expansion.library v%d\n", MIN_EXPANSION_VERSION);
        return;
    }

    debug("  boards: Scanning for ConfigDevs...\n");
    while ((cd = (struct ConfigDev *)FindConfigDev(cd, -1, -1)) != NULL) {
        if (board_list.count >= MAX_BOARDS) break;

        BoardInfo *board = &board_list.boards[board_list.count];

        board->board_address = (ULONG)cd->cd_BoardAddr;
        board->board_size = cd->cd_BoardSize;
        board->manufacturer_id = cd->cd_Rom.er_Manufacturer;
        board->product_id = cd->cd_Rom.er_Product;
        board->serial_number = cd->cd_Rom.er_SerialNumber;

        debug("  boards: Found board at $%08X\n", (ULONG)board->board_address);

        /* Determine Zorro type */
        if ((cd->cd_Rom.er_Type & ERT_TYPEMASK) == ERT_ZORROIII) {
            board->board_type = BOARD_ZORRO_III;
        } else {
            board->board_type = BOARD_ZORRO_II;
        }

        /* Format size string */
        format_board_size(board->board_size, board->size_string,
                          sizeof(board->size_string));

        /* Use identify.library to get product and manufacturer names */
        if (IdentifyBase) {
            debug("  boards: Identifying board...\n");
            IdExpansionTags(
                IDTAG_ConfigDev, (ULONG)cd,
                IDTAG_ManufStr, (ULONG)board->manufacturer_name,
                IDTAG_ProdStr, (ULONG)board->product_name,
                IDTAG_StrLength, sizeof(board->product_name) - 1,
                TAG_DONE);
        } else {
            /* Fallback if identify not available */
            snprintf(board->manufacturer_name, sizeof(board->manufacturer_name),
                     "ID %u", board->manufacturer_id);
            snprintf(board->product_name, sizeof(board->product_name),
                     "Product %u", board->product_id);
        }

        board_list.count++;
    }

    debug("  boards: Closing expansion.library...\n");
    CloseLibrary(ExpansionBase);
    debug("  boards: Enumeration complete, found %d boards\n", (LONG)board_list.count);
}

/*
 * Draw text field at position
 */
static void draw_board_field(struct RastPort *rp, WORD x, WORD y, const char *text)
{
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)text, strlen(text));
}

/*
 * Draw boards view
 */
void draw_boards_view(void)
{
    struct RastPort *rp = app->rp;
    WORD y;
    ULONG i;
    char buffer[128];

    /* Draw title panel */
    draw_panel(20,  0, 600, 24, NULL);

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 200, 14);
    Text(rp, (CONST_STRPTR)get_string(MSG_BOARDS_INFO), strlen(get_string(MSG_BOARDS_INFO)));

    /* Draw column headers */
    y = 40;
    SetAPen(rp, COLOR_TEXT);

    TightText(rp,  25, y, (CONST_STRPTR)get_string(MSG_BOARD_ADDRESS), -1, 4);
    TightText(rp, 136, y, (CONST_STRPTR)get_string(MSG_BOARD_SIZE), -1, 4);
    TightText(rp, 214, y, (CONST_STRPTR)get_string(MSG_BOARD_TYPE), -1, 4);
    TightText(rp, 296, y, (CONST_STRPTR)get_string(MSG_PRODUCT), -1, 4);
    TightText(rp, 420, y, (CONST_STRPTR)get_string(MSG_MANUFACTURER), -1, 4);
    TightText(rp, 550, y, (CONST_STRPTR)get_string(MSG_SERIAL_NO), -1, 4);

    /* Draw separator line */
    SetAPen(rp, COLOR_BUTTON_DARK);
    Move(rp, 20, y + 4);
    Draw(rp, 628, y + 4);

    /* Draw board entries */
    y = 56;
    for (i = app->board_scroll;
         i < board_list.count && y < app->screen_height - 50;
         i++) {

        BoardInfo *board = &board_list.boards[i];

        SetAPen(rp, COLOR_HIGHLIGHT);
        SetBPen(rp, COLOR_BACKGROUND);

        /* Address */
        snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)board->board_address);
        draw_board_field(rp, 25, y, buffer);

        /* Size */
        draw_board_field(rp, 136, y, board->size_string);

        /* Type */
        draw_board_field(rp, 214, y, get_board_type_string(board->board_type));

        /* Product */
        snprintf(buffer, sizeof(buffer), "%.16s", board->product_name);
        draw_board_field(rp, 296, y, buffer);

        /* Manufacturer */
        snprintf(buffer, sizeof(buffer), "%.14s", board->manufacturer_name);
        draw_board_field(rp, 420, y, buffer);

        /* Serial */
        snprintf(buffer, sizeof(buffer), "%ld", (long)board->serial_number);
        draw_board_field(rp, 550, y, buffer);

        y += 10;
    }

    if (board_list.count == 0) {
        SetAPen(rp, COLOR_TEXT);
        SetBPen(rp, COLOR_BACKGROUND);
        Move(rp, 200, 120);
        Text(rp, (CONST_STRPTR)get_string(MSG_BOARDS_NO_BOARDS_FOUND), strlen(get_string(MSG_BOARDS_NO_BOARDS_FOUND)));
    }

    /* Draw exit button */
    Button *btn = find_button(BTN_BOARD_EXIT);
    if (btn) draw_button(btn);
}

/*
 * Update buttons for Boards view
 */
void boards_view_update_buttons(void)
{
    add_button(20, 188, 60, 12,
               get_string(MSG_BTN_EXIT), BTN_BOARD_EXIT, TRUE);
}

/*
 * Handle button press for Boards view
 */
void boards_view_handle_button(ButtonID id)
{
    if (id == BTN_BOARD_EXIT) {
        switch_to_view(VIEW_MAIN);
    }
}
