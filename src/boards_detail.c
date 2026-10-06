// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Matthias Heinrichs
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/* Classic board details. Values and AutoConfig decoding are shared with MUI. */
#include "boards_detail.h"
#include "format.h"

static const BoardInfo *selected_board(void)
{
    if (app->selected_board < 0 || app->selected_board >= (LONG)board_list.count)
        return NULL;
    return &board_list.boards[app->selected_board];
}

static void draw_details(void)
{
    const BoardInfo *board = selected_board();
    ULONG row;
    char buffer[64];
    WORD y = 40;

    /* Clear labels too: PCI has fewer fields and a class instead of a serial. */
    SetAPen(app->rp, COLOR_PANEL_BG);
    RectFill(app->rp, 102, 30, 617, 183);
    if (!board) {
        draw_label_value(110, y, get_string(MSG_BOARDS_NO_BOARDS_FOUND), NULL, 0);
    } else {
        for (row = 0; row < board_detail_count(board); row++, y += 10) {
            SetAPen(app->rp, COLOR_TEXT);
            SetBPen(app->rp, COLOR_PANEL_BG);
            draw_text_clipped(110, y, get_string(board_field_label(row, board->board_type)), 182);
            SetAPen(app->rp, COLOR_HIGHLIGHT);
            draw_text_clipped(300, y,
                format_board_field(board, app->board_display, row, buffer, sizeof(buffer)), 314);
        }
    }
    for (row = BTN_BOARD_DETAIL_PREV; row <= BTN_BOARD_DETAIL_BACK; row++) {
        Button *button = find_button(row);
        if (button) {
            if (row == BTN_BOARD_DETAIL_DISPLAY) draw_cycle_button(button);
            else draw_button(button);
        }
    }
}

void draw_board_detail_view(void)
{
    draw_panel(100, 0, 520, 24, NULL);
    draw_text_centered(100, 14, 520, get_string(MSG_BOARD_DETAILS_TITLE), COLOR_TEXT);
    draw_panel(100, 28, 520, 158, NULL);
    draw_details();
}

void board_detail_view_update_buttons(void)
{
    static const LocaleStringID display_labels[BOARD_DISPLAY_COUNT] = {
        MSG_BOARD_NAMES, MSG_BOARD_DECIMAL, MSG_BOARD_HEX
    };
    static char counter[24];
    BOOL valid = selected_board() != NULL;

    snprintf(counter, sizeof(counter), "%lu / %lu",
             valid ? (unsigned long)app->selected_board + 1 : 0,
             (unsigned long)board_list.count);
    add_button(100, 188, 60, 12, get_string(MSG_BTN_PREV),
               BTN_BOARD_DETAIL_PREV, valid && app->selected_board > 0);
    add_button(166, 188, 76, 12, counter, BTN_BOARD_DETAIL_COUNTER, FALSE);
    add_button(248, 188, 60, 12, get_string(MSG_BTN_NEXT),
               BTN_BOARD_DETAIL_NEXT, valid && app->selected_board + 1 < (LONG)board_list.count);
    add_button(314, 188, 100, 12, get_string(display_labels[app->board_display]),
               BTN_BOARD_DETAIL_DISPLAY, valid);
    add_button(420, 188, 80, 12, get_string(MSG_BOARD_BACK), BTN_BOARD_DETAIL_BACK, TRUE);
}

void board_detail_view_handle_button(ButtonID id)
{
    switch (id) {
        case BTN_BOARD_DETAIL_PREV:
            if (!selected_board() || app->selected_board == 0) return;
            app->selected_board--;
            break;
        case BTN_BOARD_DETAIL_NEXT:
            if (!selected_board() || app->selected_board + 1 >= (LONG)board_list.count) return;
            app->selected_board++;
            break;
        case BTN_BOARD_DETAIL_DISPLAY:
            if (!selected_board()) return;
            app->board_display = (app->board_display + 1) % BOARD_DISPLAY_COUNT;
            break;
        case BTN_BOARD_DETAIL_BACK:
            switch_to_view(VIEW_BOARDS);
            return;
        default:
            return;
    }
    update_button_states();
    draw_details();
}
