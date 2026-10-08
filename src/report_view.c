// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/* Classic report preview. Report ownership and saving are shared with MUI. */
#include <devices/inputevent.h>
#include "report_view.h"
#include "print.h"
#include "locale_str.h"

static ReportText report;
static ReportFormat selected_format = REPORT_BRIEF;
static struct TextFont *report_font;
static ULONG first_line;
static WORD drag_y;
static ULONG drag_line;

#define REPORT_TOP 24
#define REPORT_WIDTH 612

static WORD body_height(void) { return app->screen_height - REPORT_TOP - 22; }
static ULONG visible_lines(void) { return body_height() / report_font->tf_YSize; }
static ULONG max_line(void)
{
    ULONG visible = visible_lines();
    return report.count > visible ? report.count - visible : 0;
}
static void report_error(LocaleStringID message)
{
    show_status_overlay(get_string(message));
    Delay(100);
    hide_status_overlay();
}

void close_report_view(void)
{
    free_report(&report);
    if (report_font) CloseFont(report_font);
    report_font = NULL;
}

void open_report_view(void)
{
    struct TextAttr font = { (STRPTR)"topaz.font", 8, FS_NORMAL, 0 };
    report_font = OpenFont(&font);
    if (!report_font || !create_report(&report, selected_format)) {
        close_report_view();
        report_error(MSG_REPORT_FAILED);
        return;
    }
    first_line = 0;
    switch_to_view(VIEW_REPORT);
}

void report_view_update_buttons(void)
{
    WORD bottom = app->screen_height - 18;
    add_button(4, 3, 160, 16, report_format_name(selected_format), BTN_REPORT_FORMAT, TRUE);
    add_button(388, 3, 156, 16, get_string(MSG_REPORT_SAVE_AS), BTN_REPORT_SAVE, TRUE);
    add_button(552, 3, 84, 16, get_string(MSG_CLOSE), BTN_REPORT_CLOSE, TRUE);
    add_button(4, bottom, 64, 16, get_string(MSG_BTN_PREV), BTN_REPORT_PREV, first_line > 0);
    add_button(72, bottom, 64, 16, get_string(MSG_BTN_NEXT), BTN_REPORT_NEXT, first_line < max_line());
    add_button(624, REPORT_TOP, 12, 10, NULL, BTN_REPORT_UP, first_line > 0);
    add_button(624, REPORT_TOP + body_height() - 10, 12, 10, NULL, BTN_REPORT_DOWN, first_line < max_line());
    add_button(624, REPORT_TOP + 11, 12, body_height() - 22, NULL,
               BTN_REPORT_SCROLLBAR, max_line() > 0);
}

/* JAM2 replaces the character cells and only the tail is erased, so a
 * row is never blanked before its new text appears. */
static void draw_report_row(ULONG row)
{
    struct RastPort *rp = app->rp;
    WORD top = REPORT_TOP + row * report_font->tf_YSize;
    WORD end;

    Move(rp, 4, top + report_font->tf_Baseline);
    if (first_line + row < report.count)
        draw_text_clipped(4, top + report_font->tf_Baseline,
                          report.lines[first_line + row], REPORT_WIDTH);
    end = rp->cp_x;
    SetAPen(rp, COLOR_PANEL_BG);
    if (end <= REPORT_WIDTH + 6)
        RectFill(rp, end, top, REPORT_WIDTH + 6, top + report_font->tf_YSize - 1);
    SetAPen(rp, COLOR_TEXT);
}

/* Only the text, thumb and counter change while scrolling. Short scrolls
 * move the visible rows with the blitter and draw just the exposed ones. */
static void draw_report_contents(LONG rows)
{
    struct RastPort *rp = app->rp;
    ULONG i, visible = visible_lines();
    ULONG first = 0, last = visible;
    WORD height = report_font->tf_YSize;
    char position[48];
    Button *bar = find_button(BTN_REPORT_SCROLLBAR);
    struct TextFont *saved_font = rp->Font;
    UBYTE saved_mode = rp->DrawMode;

    if (rows > 0 && (ULONG)rows < visible) {
        ClipBlit(rp, 1, REPORT_TOP + rows * height, rp, 1, REPORT_TOP,
                 REPORT_WIDTH + 6, (visible - rows) * height, 0xc0);
        first = visible - rows;
    } else if (rows < 0 && (ULONG)-rows < visible) {
        ClipBlit(rp, 1, REPORT_TOP, rp, 1, REPORT_TOP - rows * height,
                 REPORT_WIDTH + 6, (visible + rows) * height, 0xc0);
        last = -rows;
    }
    SetFont(rp, report_font);
    SetDrMd(rp, JAM2);
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    for (i = first; i < last; i++)
        draw_report_row(i);
    SetFont(rp, saved_font);
    draw_scroll_bar(bar->x, bar->y, bar->width, bar->height,
                    first_line, report.count, visible);

    snprintf(position, sizeof(position), "%lu - %lu / %lu",
        (unsigned long)first_line + 1,
        (unsigned long)(first_line + visible < report.count ? first_line + visible : report.count),
        (unsigned long)report.count);
    i = TextLength(rp, (CONST_STRPTR)position, strlen(position));
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, 220, app->screen_height - 18, 616 - i - 1, app->screen_height - 2);
    draw_text_right(220, app->screen_height - 7, 396, position, COLOR_TEXT);
    SetDrMd(rp, saved_mode);
}

void draw_report_view(void)
{
    ButtonID id;

    SetAPen(app->rp, COLOR_PANEL_BG);
    RectFill(app->rp, 0, 0, SCREEN_WIDTH - 1, app->screen_height - 1);
    draw_text_centered(168, 14, 216, get_string(MSG_REPORT_TITLE), COLOR_TEXT);
    draw_3d_box(0, REPORT_TOP - 2, REPORT_WIDTH + 8, body_height() + 4, TRUE);
    for (id = BTN_REPORT_FORMAT; id < BTN_REPORT_SCROLLBAR; id++)
        redraw_button(id);
    draw_report_contents(0);
}

static void scroll_report(LONG rows)
{
    LONG line = (LONG)first_line + rows;
    ButtonID id;

    if (line < 0) line = 0;
    if ((ULONG)line > max_line()) line = max_line();
    if ((ULONG)line == first_line) return;
    rows = line - (LONG)first_line;
    first_line = line;
    draw_report_contents(rows);
    for (id = BTN_REPORT_PREV; id <= BTN_REPORT_DOWN; id++) {
        Button *button = find_button(id);
        BOOL enabled = id == BTN_REPORT_PREV || id == BTN_REPORT_UP ?
                       first_line > 0 : first_line < max_line();
        if (button->enabled != enabled) {
            button->enabled = enabled;
            redraw_button(id);
        }
    }
}

void report_view_handle_button(ButtonID id)
{
    switch (id) {
    case BTN_REPORT_FORMAT: {
        ReportText next = { 0 };
        ReportFormat format = (selected_format + 1) % REPORT_COUNT;
        if (!create_report(&next, format)) { report_error(MSG_REPORT_FAILED); break; }
        free_report(&report);
        report = next;
        selected_format = format;
        first_line = 0;
        update_button_states();
        draw_report_view();
        break;
    }
    case BTN_REPORT_SAVE: {
        char filename[MAX_FILENAME_LEN] = DEFAULT_OUTPUT_FILE;
        if (show_filename_requester(get_string(MSG_REPORT_SAVE_AS), filename, sizeof(filename))) {
            BOOL saved = save_report(&report, filename);
            /* The filename requester restores its overlay before returning. */
            if (!saved) report_error(MSG_REPORT_SAVE_FAILED);
            else {
                char message[MAX_FILENAME_LEN + 64];
                snprintf(message, sizeof(message), get_string(MSG_REPORT_SAVED), filename);
                show_status_overlay(message);
                Delay(75);
                hide_status_overlay();
            }
        }
        break;
    }
    case BTN_REPORT_CLOSE: switch_to_view(VIEW_MAIN); break;
    case BTN_REPORT_PREV: scroll_report(-(LONG)visible_lines()); break;
    case BTN_REPORT_NEXT: scroll_report(visible_lines()); break;
    case BTN_REPORT_UP: scroll_report(-1); break;
    case BTN_REPORT_DOWN: scroll_report(1); break;
    default: break;
    }
}

void report_view_key(UWORD code, UWORD qualifier)
{
    LONG step = qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT) ? visible_lines() : 1;
    switch (code) {
    case 0x4c: scroll_report(-step); break;
    case 0x4d: scroll_report(step); break;
    case 0x48: scroll_report(-(LONG)visible_lines()); break;
    case 0x49: scroll_report(visible_lines()); break;
    case 0x7a: scroll_report(-3); break;
    case 0x7b: scroll_report(3); break;
    }
}

void report_view_scrollbar(WORD y)
{
    Button *bar = find_button(BTN_REPORT_SCROLLBAR);
    WORD knob_y, knob_h, travel;
    LONG line;
    if (!bar || !bar->enabled) return;
    scrollbar_knob(bar->height, first_line, report.count, visible_lines(), &knob_y, &knob_h);
    travel = bar->height - 2 - knob_h;
    if (travel <= 0) return;
    if (app->scrollbar_dragging) {
        line = (LONG)drag_line + ((LONG)y - drag_y) * (LONG)max_line() / travel;
        scroll_report(line - (LONG)first_line);
    } else if (y < bar->y + knob_y) {
        scroll_report(-(LONG)visible_lines());
    } else if (y >= bar->y + knob_y + knob_h) {
        scroll_report(visible_lines());
    } else {
        app->scrollbar_dragging = TRUE;
        drag_y = y;
        drag_line = first_line;
    }
}
