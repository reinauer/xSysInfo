// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - GUI rendering and event handling
 *
 * The TextLength() calls immediately before Move()/Text() are deliberate
 * workarounds for missing text on AmiKit/AfA_OS setups. Keep them even
 * when the width is unused or was measured earlier. Their necessity must
 * be checked on an affected setup before changing this call sequence.
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <graphics/gfxmacros.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "xsysinfo.h"
#include "gui.h"
#include "format.h"
#include "hardware.h"
#include "wdprobe.h"
#include "benchmark.h"
#include "software.h"
#include "memory.h"
#include "drives.h"
#include "boards.h"
#include "boards_detail.h"
#include "scsi.h"
#include "print.h"
#include "report_view.h"
#include "cache.h"
#include "clock.h"
#include "locale_str.h"

/* External references */
extern AppContext *app;
extern HardwareInfo hw_info;
extern BenchmarkResults bench_results;
extern SoftwareList libraries_list;
extern SoftwareList devices_list;
extern SoftwareList resources_list;
extern SoftwareList mmu_list;
extern MemoryRegionList memory_regions;
extern DriveList drive_list;
extern BoardList board_list;
extern struct GfxBase *GfxBase;

/* Button definitions for main view */
#define MAX_BUTTONS 32
Button buttons[MAX_BUTTONS];
int num_buttons = 0;

#define MAX_HARDWARE_ROWS MAX_HARDWARE_INFO_ROWS
typedef struct {
    const char *label;
    char value[80];
    WORD indent, offset, height, top, y, page, group;
    BOOL has_value;
    ButtonID control;
} HardwareRow;
static HardwareRow hardware_rows[MAX_HARDWARE_ROWS];
static WORD hardware_row_count, hardware_page, hardware_pages;

#define CACHE_BTN_X (HARDWARE_PANEL_X + 226)
#define CACHE_BTN_W 32
#define HARDWARE_OVERVIEW_VALUE_OFFSET 90
#define HARDWARE_CHIPSET_VALUE_OFFSET 124
#define PANEL_CYCLE_MARGIN 2
#define SOFTWARE_CYCLE_WIDTH 92

#define XSYSINFO_LOGO_W     104
#define XSYSINFO_LOGO_H     16
#define XSYSINFO_LOGO_BPR   14

static const UWORD xsysinfo_logo_template[XSYSINFO_LOGO_H][XSYSINFO_LOGO_BPR / 2] = {
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0C0C, 0x3FCC, 0x0C3F, 0xCFFC, 0x0000, 0xFC00, 0x0000 },
    { 0x0C0C, 0x3FCC, 0x0C3F, 0xCFFC, 0x0000, 0xFC00, 0x0000 },
    { 0x0661, 0x8018, 0x1980, 0x0181, 0xFE06, 0x007E, 0x0000 },
    { 0x0661, 0x8018, 0x1980, 0x0181, 0xFE06, 0x007E, 0x0000 },
    { 0x0181, 0x8018, 0x1980, 0x0181, 0x8186, 0x0181, 0x8000 },
    { 0x0303, 0x0030, 0x3300, 0x0303, 0x030C, 0x0303, 0x0000 },
    { 0x0300, 0xFC0F, 0xF0FC, 0x0303, 0x033F, 0x0303, 0x0000 },
    { 0x0300, 0xFC0F, 0xF0FC, 0x0303, 0x033F, 0x0303, 0x0000 },
    { 0x1980, 0x0600, 0x6006, 0x0606, 0x0618, 0x0606, 0x0000 },
    { 0x1980, 0x0600, 0x6006, 0x0606, 0x0618, 0x0606, 0x0000 },
    { 0x6060, 0x0660, 0x6006, 0x0606, 0x0618, 0x0606, 0x0000 },
    { 0xC0C0, 0x0CC0, 0xC00C, 0x0C0C, 0x0C30, 0x0C0C, 0x0000 },
    { 0x000F, 0xF03F, 0x0FF0, 0xFFCC, 0x0C30, 0x03F0, 0x0000 },
    { 0x000F, 0xF03F, 0x0FF0, 0xFFCC, 0x0C30, 0x03F0, 0x0000 },
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }
};

typedef struct {
    struct BitMap legacy_bitmap;
    struct BitMap *bitmap;
    WORD x, y, w, h;
    UBYTE depth;
    BOOL valid;
    BOOL allocated_bitmap;
} OverlayBackup;

static OverlayBackup overlay_backup;

/*
 * BltBitMap()/ClipBlit() use A as the mask, B as source, and C/D as
 * destination. ABC|ABNC copies source pixels through the mask.
 */
#define OVERLAY_COPY_MINTERM (ABC | ABNC)

/* Forward declarations */
static void draw_header(void);
static void draw_xsysinfo_logo(WORD x, WORD y);
static void draw_software_panel(void);
static void draw_speed_panel(void);
static void refresh_speed_panel_contents(void);
static void refresh_speed_bars(BOOL redraw_scale_button);
static void draw_hardware_panel(void);
static void draw_hardware_panel_contents(void);
static void draw_bottom_buttons(void);
static void draw_cache_buttons(void);
static void clear_buttons(void);
static void update_software_list(BOOL clear_content);
static void update_hardware_text(void);
static void refresh_all_cache_buttons(void);
static void show_status_overlay_centered(const char *message,
                                         WORD area_x, WORD area_y,
                                         WORD area_w, WORD area_h);
static void show_speed_status_overlay(const char *message);
static const char *get_hardware_page_label(void);
static void refresh_hardware_benchmark_rows(void);
static void build_hardware_rows(void);
static void add_hardware_buttons(void);

/* Shared by gradients, the logo, and TightText's ink/coverage planes.
 * UI fonts are validated by open_ui_font(): at most eight pixels high. */
#define DRAWING_TEMPLATE_SIZE (((SCREEN_WIDTH + 30) / 16) * 2 * 16)
#define TEXT_TEMPLATE_WORDS ((SCREEN_WIDTH + 15) / 16)
static UWORD *drawing_template;

/* Compose the validated UI bitmap font, preserving skipped spaces and
 * per-character overwrite order. Two masks also retain JAM1/COMPLEMENT. */
static WORD draw_tight_text(struct RastPort *rp, int x, int y,
                            CONST_STRPTR str, int charGap, int spaceWidth,
                            int right_edge, BOOL erase_spaces)
{
    const struct TextFont *font = rp->Font;
    const ULONG *locations = font->tf_CharLoc;
    const WORD *kerning = font->tf_CharKern;
    UWORD *ink = drawing_template;
    UWORD *coverage = ink + TEXT_TEMPLATE_WORDS * font->tf_YSize;
    UBYTE mode = rp->DrawMode;
    UBYTE pen = rp->FgPen;
    int currentX = x, finalX = x;
    int targetWidth = charGap < 0 ? 8 + charGap : 0;
    int left = SCREEN_WIDTH, right = 0;
    int space = TextLength(rp, (CONST_STRPTR)" ", 1);

    if (spaceWidth > space) spaceWidth = space;
    memset(ink, 0, TEXT_TEMPLATE_WORDS * sizeof(UWORD) * font->tf_YSize * 2);

    for (; *str; str++) {
        unsigned ch = (UBYTE)*str, index;
        ULONG location;
        int advance, gap = charGap, kern, width, extent, row;
        const UBYTE *glyph;

        if (ch == ' ' && !erase_spaces) {
            currentX += spaceWidth;
            continue;
        }
        advance = TextLength(rp, str, 1);
        if (right_edge >= 0 && currentX + advance > right_edge)
            break;
        if (targetWidth > 0)
            gap = advance > targetWidth ? targetWidth - advance : 0;
        index = ch >= font->tf_LoChar && ch <= font->tf_HiChar ?
                ch - font->tf_LoChar : font->tf_HiChar - font->tf_LoChar + 1U;
        location = locations[index];
        kern = kerning ? kerning[index] : 0;
        width = location & 0xffff;
        glyph = (const UBYTE *)font->tf_CharData + (location >> 19);
        if (ch == ' ' && erase_spaces) {
            width = kern = gap = 0;
        }
        extent = advance;
        /* V34 Text clips ink extending past its final pen position. */
        if (GfxBase->LibNode.lib_Version >= 36 && kern + width > extent)
            extent = kern + width;

        {
            int first = currentX < 0 ? -currentX : 0;
            int last = currentX + extent > SCREEN_WIDTH ? SCREEN_WIDTH - currentX : extent;
            int glyphFirst = first > kern ? first : kern;
            int glyphLast = last < kern + width ? last : kern + width;
            int dest = currentX + first;
            ULONG cell, keep, invert;
            ULONG *out, *covered;
            unsigned sourceBit = 0, sourceShift = 0, destShift = 0;
            ULONG sourceMask = 0;
            BOOL crossesByte = FALSE;

            if (last > first) {
                cell = ((1UL << (last - first)) - 1) << (32 - (dest & 15) - (last - first));
                keep = mode & JAM2 ? ~cell : ~0UL;
                invert = mode & INVERSVID ? cell : 0;
                /* A glyph spans at most two words. The shared allocation
                 * includes a spare word beyond the final coverage row. */
                out = (ULONG *)(ink + (dest >> 4));
                covered = (ULONG *)(coverage + (dest >> 4));
                if (glyphLast > glyphFirst) {
                    sourceBit = (location >> 16) + glyphFirst - kern;
                    glyph = (const UBYTE *)font->tf_CharData + (sourceBit >> 3);
                    sourceShift = 16 - (sourceBit & 7) - (glyphLast - glyphFirst);
                    sourceMask = (1UL << (glyphLast - glyphFirst)) - 1;
                    crossesByte = (sourceBit & 7) + glyphLast - glyphFirst > 8;
                    destShift = 32 - (dest & 15) - (glyphLast - first);
                }
                if (dest < left) left = dest;
                if (currentX + last > right) right = currentX + last;
                for (row = 0; row < font->tf_YSize; row++) {
                    ULONG bits = 0;
                    if (glyphLast > glyphFirst) {
                        bits = (UWORD)glyph[0] << 8;
                        if (crossesByte) bits |= glyph[1];
                        bits = ((bits >> sourceShift) & sourceMask) << destShift;
                        glyph += font->tf_Modulo;
                    }
                    bits ^= invert;
                    *covered |= cell;
                    if (mode & COMPLEMENT) *out ^= bits;
                    else *out = (*out & keep) | bits;
                    out = (ULONG *)((UWORD *)out + TEXT_TEMPLATE_WORDS);
                    covered = (ULONG *)((UWORD *)covered + TEXT_TEMPLATE_WORDS);
                }
            }
        }
        finalX = currentX + advance;
        currentX += advance + gap;
    }

    if (right > left) {
        WORD offset = left >> 4;
        WORD top = y - font->tf_Baseline;

        /* Coverage leaves spaces untouched. Replacing covered ink above
         * reproduces the original per-character JAM2 overlap order. */
        SetDrMd(rp, mode & COMPLEMENT ? COMPLEMENT : JAM1);
        if ((mode & JAM2) && !(mode & COMPLEMENT)) {
            SetAPen(rp, rp->BgPen);
            BltTemplate(coverage + offset, left & 15, TEXT_TEMPLATE_WORDS * 2,
                        rp, left, top, right - left, font->tf_YSize);
            SetAPen(rp, pen);
        }
        BltTemplate(ink + offset, left & 15, TEXT_TEMPLATE_WORDS * 2,
                    rp, left, top, right - left, font->tf_YSize);
        WaitBlit();
        SetDrMd(rp, mode);
    }
    Move(rp, finalX, y);
    return finalX;
}

void TightText(struct RastPort *rp, int x, int y, CONST_STRPTR str,
               int charGap, int spaceWidth)
{
    draw_tight_text(rp, x, y, str, charGap, spaceWidth, -1, FALSE);
}

/*
 * Clear all buttons
 */
static void clear_buttons(void)
{
    num_buttons = 0;
    memset(buttons, 0, sizeof(buttons));
}

/*
 * Add a button
 */
void add_button(WORD x, WORD y, WORD w, WORD h,
                       const char *label, ButtonID id, BOOL enabled)
{
    if (num_buttons >= MAX_BUTTONS) return;

    Button *btn = &buttons[num_buttons++];
    btn->x = x;
    btn->y = y;
    btn->width = w;
    btn->height = h;
    btn->label = label;
    btn->id = id;
    btn->enabled = enabled;
    btn->pressed = FALSE;
}

/*
 * Find button by ID
 */
Button *find_button(ButtonID id)
{
    int i;
    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id == id) {
            return &buttons[i];
        }
    }
    return NULL;
}

static const char *get_software_page_label(void)
{
    switch (app->software_type) {
        case SOFTWARE_LIBRARIES: return get_string(MSG_LIBRARIES);
        case SOFTWARE_DEVICES:   return get_string(MSG_DEVICES);
        case SOFTWARE_RESOURCES: return get_string(MSG_RESOURCES);
        case SOFTWARE_MMU:       return get_string(MSG_MMU_ENTRIES);
        default:                return get_string(MSG_SOFTWARE_OVERVIEW);
    }
}

static const char *get_hardware_page_label(void)
{
    switch (app->hardware_type) {
        case HARDWARE_CPU:
            return get_string(MSG_HARDWARE_CPU);
        case HARDWARE_EXT:
            return get_string(MSG_HARDWARE_EXT);
        case HARDWARE_SCSI:
            return get_string(MSG_HARDWARE_SCSI);
        case HARDWARE_CLOCK:
            return get_string(MSG_HARDWARE_CLOCK);
        case HARDWARE_STD:
        default:
            return get_string(MSG_HARDWARE_STD);
    }
}

static void set_button_enabled(ButtonID id, BOOL enabled)
{
    Button *btn = find_button(id);
    if (btn) {
        btn->enabled = enabled;
    }
}

/*
 * Set button pressed state and redraw it
 */
void set_button_pressed(ButtonID id, BOOL pressed)
{
    Button *btn = find_button(id);
    if (btn) {
        btn->pressed = pressed;
    }
}

/*
 * Redraw a specific button by ID
 */
void redraw_button(ButtonID id)
{
    Button *btn = find_button(id);
    if (!btn) return;

    /* For scroll arrows, use special drawing */
    if (id == BTN_SOFTWARE_UP || id == BTN_REPORT_UP) {
        draw_scroll_arrow(btn->x, btn->y, btn->width, btn->height,
                          TRUE, btn->pressed);
    } else if (id == BTN_SOFTWARE_DOWN || id == BTN_REPORT_DOWN) {
        draw_scroll_arrow(btn->x, btn->y, btn->width, btn->height,
                          FALSE, btn->pressed);
    } else if (id == BTN_SOFTWARE_CYCLE || id == BTN_SCALE_TOGGLE ||
               id == BTN_HARDWARE_CYCLE || id == BTN_BOARD_DISPLAY ||
               id == BTN_REPORT_FORMAT) {
        draw_cycle_button(btn);
    } else {
        draw_button(btn);
    }
}

/*
 * Update buttons for Main view
 */
void main_view_update_buttons(void)
{
    /* Bottom row buttons */
    add_button(177, 176, 60, 11,
               get_string(MSG_BTN_QUIT), BTN_QUIT, TRUE);
    add_button(239, 176, 60, 11,
               get_string(MSG_BTN_MEMORY), BTN_MEMORY, TRUE);
    add_button(177, 187, 60, 11,
               get_string(MSG_BTN_DRIVES), BTN_DRIVES, TRUE);
    add_button(301, 176, 60, 11,
               get_string(MSG_BTN_BOARDS), BTN_BOARDS, TRUE);
    add_button(239, 187, 60, 11,
               get_string(MSG_BTN_SPEED), BTN_SPEED, TRUE);
    add_button(301, 187, 60, 11,
               get_string(MSG_BTN_REPORT), BTN_REPORT, TRUE);

    /* Software type cycle button */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W -
               PANEL_CYCLE_MARGIN - SOFTWARE_CYCLE_WIDTH,
               SOFTWARE_PANEL_Y + 2, SOFTWARE_CYCLE_WIDTH, 12,
               get_software_page_label(),
               BTN_SOFTWARE_CYCLE, TRUE);

    /* Software scroll buttons (arrows on right side) */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + 15, 12, 10,
               NULL, BTN_SOFTWARE_UP,
               app->software_type != SOFTWARE_OVERVIEW);
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + 15 + 10, 12, SOFTWARE_PANEL_H - 15 - 10 - 12,
               NULL, BTN_SOFTWARE_SCROLLBAR,
               app->software_type != SOFTWARE_OVERVIEW);
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + SOFTWARE_PANEL_H - 12, 12, 10,
               NULL, BTN_SOFTWARE_DOWN,
               app->software_type != SOFTWARE_OVERVIEW);

    /* Scale toggle button */
    add_button(SPEED_PANEL_X + SPEED_PANEL_W -
               PANEL_CYCLE_MARGIN - SOFTWARE_CYCLE_WIDTH,
               SPEED_PANEL_Y + 2, SOFTWARE_CYCLE_WIDTH, 12,
               app->bar_scale == SCALE_SHRINK ?
                   get_string(MSG_SHRINK) : get_string(MSG_EXPAND),
               BTN_SCALE_TOGGLE, TRUE);

    /* Hardware type cycle button */
    add_button(HARDWARE_PANEL_X + HARDWARE_PANEL_W - PANEL_CYCLE_MARGIN - 82,
               HARDWARE_PANEL_Y + 2, 82, 12,
               get_hardware_page_label(),
               BTN_HARDWARE_CYCLE, TRUE);

    build_hardware_rows();
    add_hardware_buttons();
}

/*
 * Handle button press for Main view
 */
void main_view_handle_button(ButtonID id)
{
    switch (id) {
        case BTN_QUIT:
            app->running = FALSE;
            break;

        case BTN_MEMORY:
            switch_to_view(VIEW_MEMORY);
            break;

        case BTN_DRIVES:
            switch_to_view(VIEW_DRIVES);
            break;

        case BTN_BOARDS:
            switch_to_view(VIEW_BOARDS);
            break;

        case BTN_SPEED:
            show_speed_status_overlay(get_string(MSG_MEASURING_SPEED));
            Forbid();
            run_benchmarks();
            Permit();
            hide_status_overlay();
            update_button_states();
            refresh_hardware_benchmark_rows();
            refresh_speed_panel_contents();
            break;

        case BTN_REPORT:
            open_report_view();
            break;

        case BTN_SOFTWARE_CYCLE:
            app->software_type = (app->software_type + 1) % SOFTWARE_COUNT;
            app->software_scroll = 0;
            app->scrollbar_dragging = FALSE;
            update_software_list(TRUE);
            break;
        case BTN_HARDWARE_PREV:
            if (hardware_page > 0) hardware_page--;
            update_hardware_text();
            break;
        case BTN_HARDWARE_NEXT:
            if (hardware_page + 1 < hardware_pages) hardware_page++;
            update_hardware_text();
            break;
        case BTN_HARDWARE_CYCLE:
            hardware_page = 0;
            app->hardware_type =
                (app->hardware_type + 1) % HARDWARE_COUNT;
            if (app->hardware_type == HARDWARE_SCSI &&
                !hw_info.sdmac_present && hw_info.ncr_type == NCR_NONE)
                app->hardware_type = HARDWARE_CLOCK;
            update_hardware_text();
            break;

        case BTN_SCALE_TOGGLE:
            app->bar_scale = (app->bar_scale == SCALE_SHRINK) ?
                             SCALE_EXPAND : SCALE_SHRINK;
            refresh_speed_bars(TRUE);
            break;

        case BTN_ICACHE:
            toggle_icache();
            refresh_all_cache_buttons();
            break;

        case BTN_DCACHE:
            toggle_dcache();
            refresh_all_cache_buttons();
            break;

        case BTN_IBURST:
            toggle_iburst();
            refresh_all_cache_buttons();
            break;

        case BTN_DBURST:
            toggle_dburst();
            refresh_all_cache_buttons();
            break;

        case BTN_CBACK:
            toggle_copyback();
            refresh_all_cache_buttons();
            break;

        case BTN_SUPER_SCALAR:
            toggle_super_scalar();
            refresh_all_cache_buttons();
            break;

        case BTN_SOFTWARE_UP:
            if (app->software_scroll > 0) {
                app->software_scroll--;
                update_software_list(FALSE);
            }
            break;

        case BTN_SOFTWARE_DOWN:
            {
                SoftwareList *list = get_software_list(app->software_type);
                if (list && app->software_scroll < (LONG)list->count - SOFTWARE_LIST_LINES) {
                    app->software_scroll++;
                    update_software_list(FALSE);
                }
            }
            break;

        case BTN_SOFTWARE_SCROLLBAR:
            /* Scrollbar clicking is handled specially in handle_scrollbar_click */
            break;

        default:
            break;
    }
}

/*
 * Update button states based on current view and hardware
 */
void update_button_states(void)
{
    clear_buttons();

    switch (app->current_view) {
        case VIEW_MAIN:
            main_view_update_buttons();
            break;

        case VIEW_MEMORY:
            memory_view_update_buttons();
            break;

        case VIEW_DRIVES:
            drives_view_update_buttons();
            break;

        case VIEW_BOARDS:
            boards_view_update_buttons();
            break;

        case VIEW_BOARD_DETAILS:
            board_detail_view_update_buttons();
            break;

        case VIEW_REPORT:
            report_view_update_buttons();
            break;
        case VIEW_SCSI:
            scsi_view_update_buttons();
            break;
    }
}

/*
 * Draw the current view
 */
void redraw_current_view(void)
{
    struct RastPort *rp = app->rp;

    /* Clear background */
    SetAPen(rp, COLOR_BACKGROUND);
    RectFill(rp, 0, 0, SCREEN_WIDTH - 1, app->screen_height - 1);

    update_button_states();

    switch (app->current_view) {
        case VIEW_MAIN:
            draw_main_view();
            break;
        case VIEW_MEMORY:
            draw_memory_view();
            break;
        case VIEW_DRIVES:
            draw_drives_view();
            break;
        case VIEW_BOARDS:
            draw_boards_view();
            break;

        case VIEW_BOARD_DETAILS:
            draw_board_detail_view();
            break;
        case VIEW_REPORT:
            draw_report_view();
            break;
        case VIEW_SCSI:
            draw_scsi_view();
            break;
    }
}

/*
 * Draw main view
 */
void draw_main_view(void)
{
    draw_header();
    draw_software_panel();
    draw_speed_panel();
    draw_hardware_panel();
    draw_bottom_buttons();
}


BOOL init_drawing(void)
{
    drawing_template = AllocMem(DRAWING_TEMPLATE_SIZE, MEMF_CHIP);
    return drawing_template != NULL;
}

void cleanup_drawing(void)
{
    if (drawing_template) {
        WaitBlit();
        FreeMem(drawing_template, DRAWING_TEMPLATE_SIZE);
        drawing_template = NULL;
    }
}

static BOOL gradients_available(void)
{
    return app->screen && app->screen->BitMap.Depth >= 3;
}

#include "bayer-16x16.c"

/* Build one period of the area pattern in Chip RAM. The band boundaries
 * start at left, but RectFill's pattern bits follow raster coordinates.
 * Keep that phase when a band straddles two source words. */
static WORD build_gradient_template(WORD left, WORD top, WORD width)
{
    WORD phase = left & 15;
    WORD words = (phase + width + 15) / 16;
    UWORD low_mask = 0xffffU >> phase;
    WORD offset, row;

    memset(drawing_template, 0, words * sizeof(UWORD) * 16);
    for (offset = 0; offset < width; offset += 16) {
        WORD level = width > 1 ? ((LONG)offset * 256) / (width - 1) : 0;
        WORD word = offset / 16;
        for (row = 0; row < 16; row++) {
            UWORD pattern = bayer16x16[level][(top + row) & 15];
            drawing_template[row * words + word] |= pattern & low_mask;
            if (phase && word + 1 < words)
                drawing_template[row * words + word + 1] |= pattern & ~low_mask;
        }
    }
    return words * sizeof(UWORD);
}

/* Draw one 16-row period at a time; graphics.library handles layer clipping. */
static void draw_gradient(WORD left, WORD top, WORD width, WORD height,
                          WORD start_color, WORD end_color)
{
    struct RastPort *rp = app->rp;
    WORD stride;
    LONG row;

    if (width <= 0 || width > SCREEN_WIDTH || height <= 0)
        return;

    stride = build_gradient_template(left, top, width);
    SetDrMd(rp, JAM2);
    SetBPen(rp, start_color);
    SetAPen(rp, end_color);
    for (row = 0; row < height; row += 16) {
        WORD rows = height - row;
        if (rows > 16) rows = 16;
        BltTemplate(drawing_template, left & 15, stride, rp,
                    left, top + row, width, rows);
    }
    /* The next draw may overwrite the shared source. */
    WaitBlit();
    SetAfPt(rp, NULL, 0);
}

/* Draw two gradients meeting in the middle, including the extra column for odd widths. */
static void draw_gradient_3(WORD left, WORD top, WORD width, WORD height,
                            WORD start_color, WORD middle_color, WORD end_color)
{
    WORD left_width;

    if (width <= 0 || height <= 0)
        return;

    left_width = (width + 1) / 2;
    draw_gradient(left, top, left_width, height, start_color, middle_color);
    draw_gradient(left + left_width, top, width - left_width, height,
                  middle_color, end_color);
}

static WORD shadow_text_color(void)
{
    return app->dark_mode ? COLOR_BACKGROUND : COLOR_TEXT;
}

static void draw_xsysinfo_logo(WORD x, WORD y)
{
    struct RastPort *rp = app->rp;

    memcpy(drawing_template, xsysinfo_logo_template, sizeof(xsysinfo_logo_template));
    SetDrMd(rp, JAM1);
    SetAPen(rp, shadow_text_color());
    BltTemplate(drawing_template, 0, XSYSINFO_LOGO_BPR, rp, x + 1, y + 1,
                XSYSINFO_LOGO_W, XSYSINFO_LOGO_H);
    SetAPen(rp, COLOR_HIGHLIGHT);
    BltTemplate(drawing_template, 0, XSYSINFO_LOGO_BPR, rp, x, y,
                XSYSINFO_LOGO_W, XSYSINFO_LOGO_H);
    WaitBlit();
}

/*
 * Draw header area
 */
static void draw_header(void)
{
    struct RastPort *rp = app->rp;
    char title[128];
    char subtitle[128];
    WORD title_area_x = 120;
    WORD title_area_w = SCREEN_WIDTH - title_area_x - 8;
    WORD title_x;
    WORD subtitle_x;
    WORD title_width;
    WORD subtitle_width;
    UWORD title_len;
    UWORD subtitle_len;

    draw_panel(0, 0, 640, HEADER_HEIGHT, NULL);

    /* Title bar background with a dithered gradient, SysInfo-style */
    if (gradients_available()) {
        draw_gradient_3(1, 1, SCREEN_WIDTH - 2, HEADER_HEIGHT - 2,
                        COLOR_BAR_FILL, COLOR_BUTTON_DARK, COLOR_BAR_YOU);
    } else {
        SetAPen(rp, COLOR_BAR_FILL);
        RectFill(rp, 1, 1, SCREEN_WIDTH - 2, HEADER_HEIGHT - 2);
    }

    draw_xsysinfo_logo(7, 3);

    SetDrMd(rp, JAM1);

    /* Title text */
    snprintf(title, sizeof(title), "%s - %s", XSYSINFO_VERSION, get_string(MSG_TAGLINE));
    title_len = strlen(title);
    title_width = TextLength(rp, (CONST_STRPTR)title, title_len);
    title_x = title_area_x;
    if (title_width < title_area_w) {
        title_x += (title_area_w - title_width) / 2;
    }

    /* Subtitle, centered in the same remaining title-bar space */
    snprintf(subtitle, sizeof(subtitle), "%s https://github.com/reinauer/xsysinfo",
             get_string(MSG_CONTACT_LABEL));
    subtitle_len = strlen(subtitle);
    subtitle_width = TextLength(rp, (CONST_STRPTR)subtitle, subtitle_len);
    subtitle_x = title_area_x;
    if (subtitle_width < title_area_w) {
        subtitle_x += (title_area_w - subtitle_width) / 2;
    }

    SetAPen(rp, shadow_text_color());
    Move(rp, title_x + 1, 10);
    Text(rp, (CONST_STRPTR)title, title_len);
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, title_x, 9);
    Text(rp, (CONST_STRPTR)title, title_len);

    SetAPen(rp, shadow_text_color());
    Move(rp, subtitle_x + 1, 20);
    Text(rp, (CONST_STRPTR)subtitle, subtitle_len);
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, subtitle_x, 19);
    Text(rp, (CONST_STRPTR)subtitle, subtitle_len);

    SetDrMd(rp, JAM2);
}

/*
 * Draw 3D panel box
 */
void draw_panel(WORD x, WORD y, WORD w, WORD h, const char *title)
{
    struct RastPort *rp = app->rp;
    UWORD title_len;

    /* Panel background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x, y, x + w - 1, y + h - 1);

    /* 3D border - top/left light */
    SetAPen(rp, COLOR_BUTTON_LIGHT);
    Move(rp, x, y + h - 1);
    Draw(rp, x, y);
    Draw(rp, x + w - 1, y);

    /* 3D border - bottom/right dark */
    SetAPen(rp, COLOR_BUTTON_DARK);
    Move(rp, x + 1, y + h - 1);
    Draw(rp, x + w - 1, y + h - 1);
    Draw(rp, x + w - 1, y + 1);

    /* Title if provided */
    if (title) {
        title_len = strlen(title);

        SetAPen(rp, COLOR_TITLE_BG);
        RectFill(rp, x + 1, y + 1, x + w - 2, y + h - 2);

        if (gradients_available()) {
            /* Fade across the whole strip, right up to the title's backing. */
            draw_gradient(x + 2, y + 2, w - 4, h - 4,
                          COLOR_TITLE_BG, COLOR_BACKGROUND);
        }

        SetDrMd(rp, JAM1);
        if (title_len) {
            WORD top = y + h - 6 - rp->Font->tf_Baseline;
            WORD bottom = top + rp->Font->tf_YSize + 2;
            WORD right = x + 5 + TextLength(rp, (CONST_STRPTR)title,
                                           title_len);

            /* Cover the text and shadow, leaving the gradient in place. */
            if (top < y + 1)
                top = y + 1;
            if (bottom > y + h - 2)
                bottom = y + h - 2;
            if (right > x + w - 2)
                right = x + w - 2;
            SetAPen(rp, COLOR_TITLE_BG);
            RectFill(rp, x + 3, top, right, bottom);
        }
        SetAPen(rp, shadow_text_color());
        Move(rp, x + 5, y + h - 4);
        Text(rp, (CONST_STRPTR)title, title_len);
        SetAPen(rp, COLOR_HIGHLIGHT);
        Move(rp, x + 4, y + h - 5);
        Text(rp, (CONST_STRPTR)title, title_len);
        SetDrMd(rp, JAM2);
    }
}

/*
 * Draw a 3D recessed or raised box
 */
void draw_3d_box(WORD x, WORD y, WORD w, WORD h, BOOL recessed)
{
    struct RastPort *rp = app->rp;
    WORD top_color = recessed ? COLOR_BUTTON_DARK : COLOR_BUTTON_LIGHT;
    WORD bot_color = recessed ? COLOR_BUTTON_LIGHT : COLOR_BUTTON_DARK;

    SetAPen(rp, top_color);
    Move(rp, x, y + h - 1);
    Draw(rp, x, y);
    Draw(rp, x + w - 1, y);

    SetAPen(rp, bot_color);
    Move(rp, x + 1, y + h - 1);
    Draw(rp, x + w - 1, y + h - 1);
    Draw(rp, x + w - 1, y + 1);
}

/*
 * Draw a button
 */
void draw_button(Button *btn)
{
    struct RastPort *rp = app->rp;
    WORD text_x, text_y;
    WORD text_len;

    if (!btn) return;

    /* Button background */
    SetAPen(rp, btn->enabled ? COLOR_PANEL_BG : COLOR_BUTTON_DARK);
    RectFill(rp, btn->x, btn->y, btn->x + btn->width - 1, btn->y + btn->height - 1);

    /* 3D border */
    draw_3d_box(btn->x, btn->y, btn->width, btn->height, btn->pressed);

    /* Label - centered */
    if (btn->label) {
        text_len = strlen(btn->label);
        text_x = btn->x + (btn->width -
                 TextLength(rp, (CONST_STRPTR)btn->label, text_len)) / 2;
        text_y = btn->y + (btn->height - rp->TxHeight) / 2 + rp->TxBaseline;

        SetAPen(rp, btn->enabled ? COLOR_TEXT : COLOR_PANEL_BG);
        SetBPen(rp, btn->enabled ? COLOR_PANEL_BG : COLOR_BUTTON_DARK);
        TextLength(rp, (CONST_STRPTR)btn->label, text_len);
        Move(rp, text_x, text_y);
        Text(rp, (CONST_STRPTR)btn->label, text_len);
    }
}

/*
 * Draw a cycle button
 * Used for Libraries/Devices/Resources and Shrink/Expand toggles
 */
void draw_cycle_button(Button *btn)
{
    struct RastPort *rp = app->rp;
    WORD text_x, text_y;
    WORD text_len;
    WORD icon_x;

    if (!btn) return;

    /* Match the dark section-title strips, without the stipple. */
    SetAPen(rp, COLOR_TITLE_BG);
    RectFill(rp, btn->x, btn->y, btn->x + btn->width - 1, btn->y + btn->height - 1);

    /* Recessed 3D border */
    draw_3d_box(btn->x, btn->y, btn->width, btn->height, TRUE);

    SetDrMd(rp, JAM1);

    /* Draw the cycle marker as a crisp '>' glyph. */
    icon_x = btn->x + 4;
    text_y = btn->y + (btn->height - rp->TxHeight) / 2 + rp->TxBaseline;

    SetAPen(rp, shadow_text_color());
    TextLength(rp, (CONST_STRPTR)">", 1);
    Move(rp, icon_x + 1, text_y + 1);
    Text(rp, (CONST_STRPTR)">", 1);
    SetAPen(rp, btn->enabled ? COLOR_HIGHLIGHT : COLOR_BACKGROUND);
    TextLength(rp, (CONST_STRPTR)">", 1);
    Move(rp, icon_x, text_y);
    Text(rp, (CONST_STRPTR)">", 1);

    /* Label - left-aligned after the marker */
    if (btn->label) {
        text_len = strlen(btn->label);
        text_x = btn->x + 14;  /* After icon */

        SetAPen(rp, shadow_text_color());
        TextLength(rp, (CONST_STRPTR)btn->label, text_len);
        Move(rp, text_x + 1, text_y + 1);
        Text(rp, (CONST_STRPTR)btn->label, text_len);
        SetAPen(rp, btn->enabled ? COLOR_HIGHLIGHT : COLOR_BACKGROUND);
        TextLength(rp, (CONST_STRPTR)btn->label, text_len);
        Move(rp, text_x, text_y);
        Text(rp, (CONST_STRPTR)btn->label, text_len);
    }

    SetDrMd(rp, JAM2);
}

/*
 * Draw a scroll arrow button with triangle
 */
void draw_scroll_arrow(WORD x, WORD y, WORD w, WORD h, BOOL up, BOOL pressed)
{
    struct RastPort *rp = app->rp;
    WORD cx, cy;
    WORD arrow_h, arrow_w;

    /* Button background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x, y, x + w - 1, y + h - 1);

    /* 3D border */
    draw_3d_box(x, y, w, h, pressed);

    /* Calculate arrow center and size */
    cx = x + w / 2;
    cy = y + h / 2 - 1;

    arrow_h = (h - 4) / 2;  /* Arrow height */
    arrow_w = arrow_h;      /* Arrow width (half-width actually) */

    if (arrow_h < 2) arrow_h = 2;
    if (arrow_w < 2) arrow_w = 2;

    /* Draw filled triangle */
    SetAPen(rp, COLOR_TEXT);

    if (up) {
        /* Up arrow: triangle pointing up */
        WORD row;
        for (row = 0; row <= arrow_h; row++) {
            WORD half_width = (row * arrow_w) / arrow_h;
            WORD py = cy - arrow_h / 2 + row;
            if (half_width > 0) {
                Move(rp, cx - half_width, py);
                Draw(rp, cx + half_width, py);
            } else {
                WritePixel(rp, cx, py);
            }
        }
    } else {
        /* Down arrow: triangle pointing down */
        WORD row;
        for (row = 0; row <= arrow_h; row++) {
            WORD half_width = ((arrow_h - row) * arrow_w) / arrow_h;
            WORD py = cy - arrow_h / 2 + row;
            if (half_width > 0) {
                Move(rp, cx - half_width, py);
                Draw(rp, cx + half_width, py);
            } else {
                WritePixel(rp, cx, py);
            }
        }
    }
}

/* Shared by drawing and hit testing; offset includes the track border. */
void scrollbar_knob(WORD h, ULONG pos, ULONG total, ULONG visible,
                           WORD *offset, WORD *size)
{
    WORD inner_h = h - 2;

    *offset = 1;
    *size = inner_h;
    if (total <= visible) {
        return;
    }

    *size = (visible * inner_h) / total;
    if (*size < 8) *size = 8;
    if (*size > inner_h) *size = inner_h;
    *offset += (pos * (inner_h - *size)) / (total - visible);
}

/*
 * Draw a scroll bar (prop gadget style)
 */
void draw_scroll_bar(WORD x, WORD y, WORD w, WORD h, ULONG pos, ULONG total, ULONG visible)
{
    struct RastPort *rp = app->rp;
    WORD knob_y, knob_h;

    scrollbar_knob(h, pos, total, visible, &knob_y, &knob_h);
    knob_y += y;

    /* Clear only the exposed track, so the knob is never blanked first. */
    SetAPen(rp, COLOR_BUTTON_DARK);
    if (knob_y > y + 1)
        RectFill(rp, x + 1, y + 1, x + w - 2, knob_y - 1);
    if (knob_y + knob_h < y + h - 1)
        RectFill(rp, x + 1, knob_y + knob_h, x + w - 2, y + h - 2);
    draw_3d_box(x, y, w, h, TRUE);

    /* Draw knob background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x + 1, knob_y, x + w - 2, knob_y + knob_h - 1);

    /* Draw raised 3D border on knob */
    draw_3d_box(x + 1, knob_y, w - 2, knob_h, FALSE);
}

/*
 * Draw text at position
 */
/* Explicit spans keep the compatibility sequence in one place. */
static void draw_text_span(WORD x, WORD y, const char *text, WORD len,
                           UBYTE color)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, color);
    SetBPen(rp, COLOR_PANEL_BG);
    TextLength(rp, (CONST_STRPTR)text, len);
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)text, len);
}

void draw_text(WORD x, WORD y, const char *text, UBYTE color)
{
    draw_text_span(x, y, text, strlen(text), color);
}

void draw_text_right(WORD x, WORD y, WORD width, const char *text, UBYTE color)
{
    WORD len = strlen(text);
    WORD text_x = x + width - TextLength(app->rp, (CONST_STRPTR)text, len);

    draw_text_span(text_x, y, text, len, color);
}

void draw_text_centered(WORD x, WORD y, WORD width, const char *text, UBYTE color)
{
    WORD len = strlen(text);
    WORD text_x = x + (width - TextLength(app->rp, (CONST_STRPTR)text, len)) / 2;

    draw_text_span(text_x, y, text, len, color);
}

/*
 * Draw label: value pair
 * If value is NULL, only the label is drawn
 */
/*
 * Draw text hard-clipped to max_width pixels using the current pens.
 * Measures with TextLength() so it also clips when a font replacement
 * system substitutes a wider font (issue #29).
 */
void draw_text_clipped(WORD x, WORD y, const char *text, WORD max_width)
{
    struct RastPort *rp = app->rp;
    WORD len = strlen(text);

    while (len > 0 &&
           TextLength(rp, (CONST_STRPTR)text, len) > max_width) {
        len--;
    }
    if (len > 0) {
        Move(rp, x, y);
        Text(rp, (CONST_STRPTR)text, len);
    }
}

/*
 * Draw a label/value pair with both parts clipped at the max_x column
 * (typically the enclosing panel's inner right edge).
 */
void draw_label_value_max(WORD x, WORD y, const char *label,
                          const char *value, WORD offset, WORD max_x)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_text_clipped(x, y, label, max_x - x);

    if (value) {
        SetAPen(rp, COLOR_HIGHLIGHT);
        draw_text_clipped(x + offset, y, value, max_x - (x + offset));
    }
}

void draw_label_value(WORD x, WORD y, const char *label, const char *value, WORD offset)
{
    draw_label_value_max(x, y, label, value, offset, SCREEN_WIDTH - 4);
}

static BOOL hardware_cache_enabled(ButtonID id)
{
    return cache_setting_enabled(id - BTN_ICACHE + CACHE_ICACHE);
}

/* Keep sections on one page when they fit. Oversized sections may span pages. */
static void layout_hardware_rows(void)
{
    WORD i, j, height = 0, available, used = 0, page = 0;
    WORD top = HARDWARE_PANEL_Y + 18;
    WORD bottom = HARDWARE_PANEL_Y + HARDWARE_PANEL_H - 3;
    for (i = 0; i < hardware_row_count; i++) height += hardware_rows[i].height;
    available = bottom - top + 1;
    if (height > available) available -= app->rp->TxHeight + 6;
    for (i = 0; i < hardware_row_count; i++) {
        HardwareRow *row = &hardware_rows[i];
        if (!i || row->group != hardware_rows[i - 1].group) {
            WORD section = 0;
            for (j = i; j < hardware_row_count && hardware_rows[j].group == row->group; j++)
                section += hardware_rows[j].height;
            if (used && section <= available && used + section > available) {
                page++;
                used = 0;
            }
        }
        if (used && used + row->height > available) {
            page++;
            used = 0;
        }
        row->page = page;
        row->top = top + used;
        row->y = row->top + (row->height - app->rp->TxHeight) / 2 + app->rp->TxBaseline;
        used += row->height;
    }
    hardware_pages = page + 1;
    if (hardware_page >= hardware_pages) hardware_page = hardware_pages - 1;

    /* Align columns per section using actual font widths. Preserve the current
     * minimum column, but leave enough room for the longest value where possible. */
    for (i = 0; i < hardware_row_count; i = j) {
        WORD column = 0, value_width = 0, limit;
        for (j = i; j < hardware_row_count && hardware_rows[j].group == hardware_rows[i].group; j++) {
            HardwareRow *row = &hardware_rows[j];
            WORD width;
            if (!row->has_value) continue;
            width = row->indent + TextLength(app->rp, (CONST_STRPTR)row->label, strlen(row->label)) + 4;
            if (width > column) column = width;
            if (row->indent + row->offset > column) column = row->indent + row->offset;
            width = TextLength(app->rp, (CONST_STRPTR)row->value, strlen(row->value));
            if (width > value_width) value_width = width;
        }
        limit = HARDWARE_PANEL_W - 12 - value_width;
        if (limit < 64) limit = 64;
        if (column > limit) column = limit;
        for (; i < j; i++)
            if (hardware_rows[i].has_value)
                hardware_rows[i].offset = column - hardware_rows[i].indent;
    }
}

static void draw_hardware_row(HardwareRow *row, BOOL clear)
{
    WORD x = HARDWARE_PANEL_X + 4 + row->indent;
    WORD right = HARDWARE_PANEL_X + HARDWARE_PANEL_W - 4;
    struct RastPort *rp = app->rp;
    if (clear) {
        SetAPen(rp, COLOR_PANEL_BG);
        RectFill(rp, HARDWARE_PANEL_X + 2, row->top, right, row->top + row->height - 1);
    }
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_text_clipped(x, row->y, row->label,
                      (row->control ? CACHE_BTN_X - 4 : row->has_value ? x + row->offset - 4 : right) - x);
    if (row->has_value) {
        SetAPen(rp, COLOR_HIGHLIGHT);
        draw_text_clipped(x + row->offset, row->y, row->value, right - x - row->offset);
    }
}

static void add_hardware_buttons(void)
{
    WORD i;
    for (i = 0; i < hardware_row_count; i++) {
        HardwareRow *row = &hardware_rows[i];
        if (row->page == hardware_page && row->control) {
            add_button(CACHE_BTN_X, row->top, CACHE_BTN_W, row->height,
                       row->value, row->control, TRUE);
            set_button_pressed(row->control, hardware_cache_enabled(row->control));
        }
    }
    if (hardware_pages > 1) {
        WORD y = HARDWARE_PANEL_Y + HARDWARE_PANEL_H - app->rp->TxHeight - 5;
        add_button(HARDWARE_PANEL_X + 4, y, 28, app->rp->TxHeight + 3,
                   "<", BTN_HARDWARE_PREV, hardware_page > 0);
        add_button(HARDWARE_PANEL_X + HARDWARE_PANEL_W - 32, y, 28, app->rp->TxHeight + 3,
                   ">", BTN_HARDWARE_NEXT, hardware_page + 1 < hardware_pages);
    }
}

static void draw_hardware_panel_contents(void)
{
    WORD i;
    char page[16];
    /* Buttons and rows were built together by init_main_buttons(). */
    for (i = 0; i < hardware_row_count; i++)
        if (hardware_rows[i].page == hardware_page)
            draw_hardware_row(&hardware_rows[i], FALSE);
    draw_cache_buttons();
    if (hardware_pages > 1) {
        snprintf(page, sizeof(page), "%u / %u", hardware_page + 1, hardware_pages);
        SetAPen(app->rp, COLOR_TEXT);
        Move(app->rp, HARDWARE_PANEL_X + (HARDWARE_PANEL_W - TextLength(app->rp, (CONST_STRPTR)page, strlen(page))) / 2,
             HARDWARE_PANEL_Y + HARDWARE_PANEL_H - 5);
        Text(app->rp, (CONST_STRPTR)page, strlen(page));
        redraw_button(BTN_HARDWARE_PREV);
        redraw_button(BTN_HARDWARE_NEXT);
    }
}

static void refresh_hardware_benchmark_rows(void)
{
    if (app->current_view == VIEW_MAIN)
        update_hardware_text();
}

/*
 * Draw software panel (overview/libraries/devices/resources/MMU)
 */
static void draw_software_panel(void)
{
    draw_panel(SOFTWARE_PANEL_X, SOFTWARE_PANEL_Y,
               SOFTWARE_PANEL_W, SOFTWARE_PANEL_H,
               NULL);
    draw_panel(SOFTWARE_PANEL_X + 1, SOFTWARE_PANEL_Y + 1,
               SOFTWARE_PANEL_W - 2, 14,
           get_string(MSG_SYSTEM_SOFTWARE));

    /* Draw cycle button initially */
    Button *cycle_btn = find_button(BTN_SOFTWARE_CYCLE);
    if (cycle_btn) {
        draw_cycle_button(cycle_btn);
    }

    update_software_list(TRUE);
}

/*
 * Rebuild hardware-page buttons and redraw only the changing contents.
 */
static void update_hardware_text(void)
{
    Button *hw_cycle_btn;

    update_button_states();
    hw_cycle_btn = find_button(BTN_HARDWARE_CYCLE);

    if (hw_cycle_btn) {
        draw_cycle_button(hw_cycle_btn);
    }

    SetAPen(app->rp, COLOR_PANEL_BG);
    RectFill(app->rp, HARDWARE_PANEL_X + 2, HARDWARE_PANEL_Y + 15,
             HARDWARE_PANEL_X + HARDWARE_PANEL_W - 3,
             HARDWARE_PANEL_Y + HARDWARE_PANEL_H - 2);
    draw_hardware_panel_contents();
}

static void draw_software_overview(void)
{
    char buffer[80];
    ULONG row;
    WORD y = SOFTWARE_PANEL_Y + 22;

    for (row = 0; row < software_overview_count(); row++) {
        const char *value = format_software_overview_value(row, buffer,
                                                          sizeof(buffer));
        draw_label_value_max(SOFTWARE_PANEL_X + 4, y,
                             get_string(software_overview_label(row)), value,
                             100, SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 4);
        y += TEXT_LINE_HEIGHT;
    }
}

/* Erase unused parts of a text field, preserving its foreground pen. */
static void clear_software_text(WORD x, WORD y, WORD width)
{
    struct RastPort *rp = app->rp;
    UBYTE pen = rp->FgPen;
    WORD top = y - rp->TxBaseline;

    if (width <= 0) return;
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x, top, x + width - 1, top + rp->TxHeight - 1);
    SetAPen(rp, pen);
}

/*
 * JAM2 replaces the character cells; only spaces and the unused tail need
 * explicit erasing. The MMU page uses the same tight spacing as TightText(),
 * but must paint its spaces too and stay inside the list's right edge.
 */
static void draw_software_field(WORD x, WORD y, const char *text,
                                WORD width, BOOL tight)
{
    struct RastPort *rp = app->rp;
    WORD right = x + width;
    WORD end = x;

    if (tight) {
        end = draw_tight_text(rp, x, y, (CONST_STRPTR)text, -1, 0,
                              right, TRUE);
    } else {
        Move(rp, x, y);
        draw_text_clipped(x, y, text, width);
        end = rp->cp_x;
    }
    clear_software_text(end, y, right - end);
}

/* Clear on page changes; overwrite text in place while scrolling. */
static void update_software_list(BOOL clear_content)
{
    struct RastPort *rp = app->rp;
    SoftwareList *list = get_software_list(app->software_type);
    ULONG i;
    WORD y;
    WORD list_top = SOFTWARE_PANEL_Y + 22;
    char buffer[128];

    if (clear_content) {
        /* Page layouts differ, including whether scroll controls are shown. */
        SetAPen(rp, COLOR_PANEL_BG);
        RectFill(rp, SOFTWARE_PANEL_X + 2, list_top - 7,
                 SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 3,
                 SOFTWARE_PANEL_Y + SOFTWARE_PANEL_H - 2);
    }

    /* Update cycle button only if label changed */
    Button *cycle_btn = find_button(BTN_SOFTWARE_CYCLE);
    if (cycle_btn) {
        const char *new_label = get_software_page_label();
        if (cycle_btn->label != new_label) {
            cycle_btn->label = new_label;
            draw_cycle_button(cycle_btn);
        }
    }

    set_button_enabled(BTN_SOFTWARE_UP, list != NULL);
    set_button_enabled(BTN_SOFTWARE_DOWN, list != NULL);
    set_button_enabled(BTN_SOFTWARE_SCROLLBAR, list != NULL);
    if (app->software_type == SOFTWARE_OVERVIEW) {
        draw_software_overview();
        return;
    }
    if (!list)
        return;

    /* Draw scroll arrows with triangles */
    Button *up_btn = find_button(BTN_SOFTWARE_UP);
    Button *down_btn = find_button(BTN_SOFTWARE_DOWN);
    Button *scrollbar_btn = find_button(BTN_SOFTWARE_SCROLLBAR);

    if (clear_content && up_btn) {
        draw_scroll_arrow(up_btn->x, up_btn->y, up_btn->width, up_btn->height,
                          TRUE, up_btn->pressed);
    }
    if (clear_content && down_btn) {
        draw_scroll_arrow(down_btn->x, down_btn->y, down_btn->width, down_btn->height,
                          FALSE, down_btn->pressed);
    }

    /* Draw scroll bar */
    if (scrollbar_btn) {
        draw_scroll_bar(scrollbar_btn->x, scrollbar_btn->y,
                        scrollbar_btn->width, scrollbar_btn->height,
                        app->software_scroll, list->count, SOFTWARE_LIST_LINES);
    }

    /* Draw list entries */
    SetDrMd(rp, JAM2);
    SetBPen(rp, COLOR_PANEL_BG);
    y = list_top;
    for (i = app->software_scroll;
         i < list->count && i < (ULONG)(app->software_scroll + SOFTWARE_LIST_LINES);
         i++) {

        SoftwareEntry *entry = &list->entries[i];

        if (app->software_type == SOFTWARE_MMU) {
            snprintf(buffer, 50, "%.49s", entry->name);
            if (strlen(entry->name) > 49) {
                buffer[48] = '+';
            }
            SetAPen(rp, COLOR_TEXT);
            draw_software_field(SOFTWARE_PANEL_X + 4, y, buffer,
                                SOFTWARE_PANEL_W - 20, TRUE);
        }
        else {
            /* Name */
            SetAPen(rp, COLOR_TEXT);
            draw_software_field(SOFTWARE_PANEL_X + 4, y, entry->name,
                                126 - 4, FALSE);

            /* Location */
            draw_software_field(SOFTWARE_PANEL_X + 126, y,
                                get_location_string(entry->location),
                                200 - 126, FALSE);

            /* Address */
            snprintf(buffer, 12, "$%08lX", (unsigned long)entry->address);
            SetAPen(rp, COLOR_HIGHLIGHT);
            draw_software_field(SOFTWARE_PANEL_X + 200, y, buffer,
                                284 - 200, FALSE);

            /* Version */
            snprintf(buffer, sizeof(buffer), "V%d.%d", entry->version, entry->revision);
            draw_software_field(SOFTWARE_PANEL_X + 284, y, buffer,
                                (SOFTWARE_PANEL_W - 16) - 284, FALSE);
        }

        y += 8;
    }
}

/*
 * Map a dhrystone value to a bar width in pixels. Shared by the bars and
 * the ruler ticks so both always agree, including the piecewise-linear
 * Shrink mode. May return more than SPEED_BAR_MAX_WIDTH (overflow).
 */
static ULONG scale_bar_width(ULONG value, ULONG max_value)
{
    return scale_speed_value(value, max_value, SPEED_BAR_MAX_WIDTH,
                             app->bar_scale);
}

/*
 * 3x5 micro digits for the ruler above the speed bars, like the original
 * SysInfo ruler. Rows top to bottom, bits 2..0 = left to right.
 * Index 10 is 'K' (thousands suffix).
 */
static const UBYTE micro_glyphs[11][5] = {
    { 7, 5, 5, 5, 7 },  /* 0 */
    { 2, 6, 2, 2, 7 },  /* 1 */
    { 7, 1, 7, 4, 7 },  /* 2 */
    { 7, 1, 7, 1, 7 },  /* 3 */
    { 5, 5, 7, 1, 1 },  /* 4 */
    { 7, 4, 7, 1, 7 },  /* 5 */
    { 7, 4, 7, 5, 7 },  /* 6 */
    { 7, 1, 1, 2, 2 },  /* 7 */
    { 7, 5, 7, 5, 7 },  /* 8 */
    { 7, 5, 7, 1, 7 },  /* 9 */
    { 5, 6, 4, 6, 5 },  /* K */
};

/* The ruler shares the drawing scratch; each mask is five rows high. */
static void clear_ruler_span(UWORD *bits, WORD x, WORD width)
{
    while (width > 0) {
        WORD phase = x & 15;
        WORD count = 16 - phase;
        WORD row;
        UWORD keep;

        if (count > width) count = width;
        keep = ~((0xffffU >> phase) & (0xffffU << (16 - phase - count)));
        for (row = 0; row < 5; row++)
            bits[row * TEXT_TEMPLATE_WORDS + (x >> 4)] &= keep;
        x += count;
        width -= count;
    }
}

static WORD draw_micro_glyph(WORD x, int glyph)
{
    WORD row;

    for (row = 0; row < 5; row++) {
        ULONG *out = (ULONG *)(drawing_template +
                              row * TEXT_TEMPLATE_WORDS + (x >> 4));
        *out |= (ULONG)micro_glyphs[glyph][row] << (29 - (x & 15));
    }
    return x + 4;
}

static void draw_micro_number(WORD x, ULONG value, BOOL kilo)
{
    char buf[12];
    int i;

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    for (i = 0; buf[i]; i++) {
        x = draw_micro_glyph(x, buf[i] - '0');
    }
    if (kilo && value > 0) {
        draw_micro_glyph(x, 10);
    }
}

/*
 * Draw the ruler band between the title strip and the first bar:
 * one 5px row where micro-digit labels interrupt a dotted tick line,
 * like the original SysInfo ruler.
 */
static void draw_speed_ruler(ULONG max_value)
{
    struct RastPort *rp = app->rp;
    WORD x0 = SPEED_PANEL_X + 178;
    WORD y = SPEED_PANEL_Y + 15;
    ULONG lab_max, mag, q, step, v;
    BOOL kilo;
    WORD x, last_tick = x0;
    UWORD *dots = drawing_template + TEXT_TEMPLATE_WORDS * 5;

    /* Clear the band */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x0 - 1, y, x0 + SPEED_BAR_MAX_WIDTH, y + 4);

    if (max_value == 0) return;

    /* Largest value still on the linear part of the scale */
    if (app->bar_scale == SCALE_EXPAND) {
        lab_max = max_value;
    } else {
        lab_max = reference_systems[REF_A3000].dhrystones;
    }
    if (lab_max == 0) return;

    /* Nice 1-2-5 step giving roughly 4-7 labelled ticks */
    mag = 1;
    while (lab_max / mag >= 10) mag *= 10;
    q = lab_max / mag;
    if (q >= 8) {
        step = 2 * mag;
    } else if (q >= 4) {
        step = mag;
    } else {
        step = mag / 2;
    }
    if (step == 0) step = 1;
    kilo = (step >= 1000 && step % 1000 == 0);

    SetDrMd(rp, JAM1);

    memset(drawing_template, 0, TEXT_TEMPLATE_WORDS * sizeof(UWORD) * 10);

    /* Dotted baseline across the bar width */
    for (x = x0; x < x0 + SPEED_BAR_MAX_WIDTH; x += 4) {
        dots[4 * TEXT_TEMPLATE_WORDS + (x >> 4)] |= 0x8000U >> (x & 15);
    }

    for (v = 0; ; v += step) {
        ULONG w = scale_bar_width(v, max_value);
        ULONG label = kilo ? v / 1000 : v;
        char buf[12];
        WORD label_w;

        if (w >= SPEED_BAR_MAX_WIDTH) break;
        x = x0 + (WORD)w;

        /* Tick mark */
        {
            WORD row;
            for (row = 2; row < 5; row++)
                drawing_template[row * TEXT_TEMPLATE_WORDS + (x >> 4)] |=
                    0x8000U >> (x & 15);
            last_tick = x;
        }

        /* Label to the right of the tick, interrupting the dotted line */
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)label);
        label_w = strlen(buf) * 4 + ((kilo && label) ? 4 : 0);
        if (x + 3 + label_w <= x0 + SPEED_BAR_MAX_WIDTH) {
            clear_ruler_span(drawing_template, x + 2, label_w + 1);
            clear_ruler_span(dots, x + 2, label_w + 1);
            draw_micro_number(x + 3, label, kilo);
        }
    }

    SetAPen(rp, COLOR_BUTTON_DARK);
    BltTemplate(dots + (x0 >> 4), x0 & 15, TEXT_TEMPLATE_WORDS * 2,
                rp, x0, y, SPEED_BAR_MAX_WIDTH, 5);
    SetAPen(rp, COLOR_TEXT);
    BltTemplate(drawing_template + (x0 >> 4), x0 & 15, TEXT_TEMPLATE_WORDS * 2,
                rp, x0, y, SPEED_BAR_MAX_WIDTH, 5);
    WaitBlit();
    Move(rp, last_tick, y + 4);
    SetDrMd(rp, JAM2);
}

/*
 * Draw single speed bar
 */
void draw_single_bar(WORD x, WORD y, ULONG value, ULONG max_value, WORD color)
{
    struct RastPort *rp = app->rp;
    WORD bar_width;
    BOOL overflow = FALSE;
    ULONG calculated_width = 0;

    /* Draw border */
    draw_3d_box(x - 1, y - 1, SPEED_BAR_MAX_WIDTH + 2, SPEED_BAR_HEIGHT + 2, TRUE);

    /* Clear bar interior */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x, y, x + SPEED_BAR_MAX_WIDTH - 1, y + SPEED_BAR_HEIGHT - 1);

    if (max_value == 0 || value == 0) return;

    calculated_width = scale_bar_width(value, max_value);

    /* Clamp to max width and flag values beyond the current scale */
    if (calculated_width > SPEED_BAR_MAX_WIDTH) {
        overflow = TRUE;
        bar_width = SPEED_BAR_MAX_WIDTH;
    } else {
        bar_width = calculated_width;
        if (value > max_value) {
            overflow = TRUE;
        }
    }

    /* Draw bar */
    if (bar_width > 0) {
        SetAPen(rp, color);
        RectFill(rp, x, y, x + bar_width - 1, y + SPEED_BAR_HEIGHT - 1);
    }

    /* Indicate values that exceed the current scale */
    if (overflow) {
        WORD plus_center_x = x + SPEED_BAR_MAX_WIDTH - 7;
        WORD plus_center_y = y + (SPEED_BAR_HEIGHT / 2) - 1;

        SetAPen(rp, COLOR_HIGHLIGHT);
    // -
        Move(rp, plus_center_x - 5, plus_center_y);
        Draw(rp, plus_center_x + 4, plus_center_y);
        // | needs a double line
        Move(rp, plus_center_x, plus_center_y - 2);
        Draw(rp, plus_center_x, plus_center_y + 2);
        Move(rp, plus_center_x - 1, plus_center_y - 2);
        Draw(rp, plus_center_x - 1, plus_center_y + 2);
    }
}

/*
 * Refresh speed bars only (for scale toggle without full redraw)
 */
static void refresh_speed_bars(BOOL redraw_scale_button)
{
    WORD y;
    ULONG max_value, cur_value;
    int i;

    /* Update scale toggle button */
    if (redraw_scale_button) {
        Button *scale_btn = find_button(BTN_SCALE_TOGGLE);
        if (scale_btn) {
            scale_btn->label = app->bar_scale == SCALE_SHRINK ?
                               get_string(MSG_SHRINK) :
                               get_string(MSG_EXPAND);
            draw_cycle_button(scale_btn);
        }
    }

    max_value = speed_scale_max(app->bar_scale);

    /* Ruler above the bars, same scale mapping as the bars */
    draw_speed_ruler(max_value);

    /* Redraw "You" bar */
    y = SPEED_PANEL_Y + 26;
    if (bench_results.benchmarks_valid) {
        cur_value = bench_results.dhrystones;
    } else {
        cur_value = 0;
    }
    draw_single_bar(SPEED_PANEL_X + 178, y - 5,
                    cur_value, max_value, COLOR_BAR_YOU);

    /* Redraw reference system bars */
    y += 8;
    for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
        if (bench_results.benchmarks_valid) {
            cur_value = reference_systems[i].dhrystones;
        } else {
            cur_value = 0;
        }
        draw_single_bar(SPEED_PANEL_X + 178, y - 5,
                        cur_value, max_value, COLOR_BAR_FILL);
        y += 8;
    }
}

/*
 * Draw speed comparison panel contents below the title strip.
 */
static void draw_speed_panel_contents(BOOL redraw_scale_button)
{
    struct RastPort *rp = app->rp;
    WORD y;
    char buffer[64];
    int i;

    /* Draw "You" entry first (below the ruler band) */
    y = SPEED_PANEL_Y + 26;
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_DHRYSTONES));
    draw_text_clipped(SPEED_PANEL_X + 4, y, buffer, 90 - 4);

    if (bench_results.benchmarks_valid) {
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)bench_results.dhrystones);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
    }
    SetAPen(rp, COLOR_HIGHLIGHT);
    draw_text_clipped(SPEED_PANEL_X + 90, y, buffer, 150 - 90);

    SetAPen(rp, COLOR_HIGHLIGHT);
    draw_text_clipped(SPEED_PANEL_X + 150, y, get_string(MSG_REF_YOU),
                      178 - 150);

    /* Draw reference systems labels and speed factors */
    y += 8;
    for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
        char ref_label[24];
        format_reference_label(ref_label, sizeof(ref_label), &reference_systems[i]);

        SetAPen(rp, COLOR_TEXT);
        TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)ref_label, -1, 4);

        /* Draw speed factor (your speed / reference speed) */
        if (bench_results.benchmarks_valid && reference_systems[i].dhrystones > 0) {
            ULONG factor_x100 = (bench_results.dhrystones * 100) / reference_systems[i].dhrystones;
            char factor_str[16];
            int factor_off = 0;
            BOOL round_factor = (factor_x100 >= 100000);

            if (factor_x100 <= 100000) factor_str[factor_off++] = ' ';
            if (factor_x100 <= 10000) factor_str[factor_off++] = ' ';
            if (factor_x100 <= 1000) factor_str[factor_off++] = ' ';
            format_scaled(factor_str + factor_off, sizeof(factor_str) - factor_off,
                          factor_x100, round_factor);
            SetAPen(rp, COLOR_HIGHLIGHT);
            TightText(rp, SPEED_PANEL_X + 125, y, (CONST_STRPTR)factor_str, -1, 7);
        }

        y += 8;
    }

    /* Draw cycle button and all speed bars */
    refresh_speed_bars(redraw_scale_button);

    /* MIPS and MFLOPS */
    snprintf(buffer, sizeof(buffer), "%s ", get_string(MSG_MIPS));
    SetAPen(rp, COLOR_TEXT);
    TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 4);

    if (bench_results.benchmarks_valid) {
        char scaled[16];
        format_scaled(scaled, sizeof(scaled), bench_results.mips, TRUE);
        snprintf(buffer, sizeof(buffer), "%s", scaled);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
    }
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, SPEED_PANEL_X + 40, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
    SetAPen(rp, COLOR_TEXT);
    snprintf(buffer, sizeof(buffer), "%s ",
                 get_string(MSG_MFLOPS));
    /* Starts past the Mips value's worst case ("99.99" ends at x+80) */
    TightText(rp, SPEED_PANEL_X + 84, y, (CONST_STRPTR)buffer, -1, 4);
    if (hw_info.fpu_type != FPU_NONE && bench_results.benchmarks_valid && hw_info.fpu_enabled) {
        char scaled[16];
        format_scaled(scaled, sizeof(scaled), bench_results.mflops, TRUE);
        snprintf(buffer, sizeof(buffer), "%s", scaled);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
    }
    SetAPen(rp, COLOR_HIGHLIGHT);
    TightText(rp, SPEED_PANEL_X + 132, y, (CONST_STRPTR)buffer, -1, 4);

    /* Memory speeds: labels and values share fixed column positions. */
    y += 8;
    {
        char chip_str[8], fast_str[8], rom_str[8];
        const char *values[] = { chip_str, fast_str, rom_str };
        char *label;
        WORD x = SPEED_PANEL_X + 4;
        /* Five characters at seven pixels each, plus a column gap. */
        const WORD column_width = 42;

        /* Format CHIP speed in MB/s */
        if (bench_results.benchmarks_valid && bench_results.chip_speed > 0) {
            format_scaled(chip_str, sizeof(chip_str), bench_results.chip_speed / 10000, TRUE);
        } else {
            snprintf(chip_str, sizeof(chip_str), "%s", get_string(MSG_NA));
        }

        /* Format FAST speed in MB/s or N/A */
        if (bench_results.benchmarks_valid && bench_results.fast_speed > 0) {
            format_scaled(fast_str, sizeof(fast_str), bench_results.fast_speed / 10000, TRUE);
        } else {
            snprintf(fast_str, sizeof(fast_str), "%s", get_string(MSG_NA));
        }

        /* Format ROM speed in MB/s */
        if (bench_results.benchmarks_valid && bench_results.rom_speed > 0) {
            format_scaled(rom_str, sizeof(rom_str), bench_results.rom_speed / 10000, TRUE);
        } else {
            snprintf(rom_str, sizeof(rom_str), "%s", get_string(MSG_NA));
        }

        snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_MEM_SPEED_HEADER));
        label = buffer;
        for (i = 0; i < 3; i++) {
            char *next;

            /* Catalog headers contain three space-separated labels. */
            label += strspn(label, " ");
            next = label + strcspn(label, " ");
            if (*next) *next++ = '\0';

            SetAPen(rp, COLOR_TEXT);
            TightText(rp, x, y, (CONST_STRPTR)label, -1, 4);
            SetAPen(rp, COLOR_HIGHLIGHT);
            TightText(rp, x, y + 8, (CONST_STRPTR)values[i], -1, 4);
            label = next;
            x += column_width;
        }
        TightText(rp, x, y + 8,
                  (CONST_STRPTR)get_string(MSG_MEM_SPEED_UNIT), -1, 4);
    }
}

static void refresh_speed_panel_contents(void)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, COLOR_PANEL_BG);
    /* Bottom buttons start at x=177/y=176; leave that area intact. */
    RectFill(rp, SPEED_PANEL_X + 1, SPEED_PANEL_Y + 15,
             SPEED_PANEL_X + SPEED_PANEL_W - 2,
             SPEED_PANEL_Y + 77);
    RectFill(rp, SPEED_PANEL_X + 1, SPEED_PANEL_Y + 78,
             SPEED_PANEL_X + 176, SPEED_PANEL_Y + SPEED_PANEL_H - 2);
    draw_speed_panel_contents(FALSE);
}

/*
 * Draw speed comparison panel
 */
static void draw_speed_panel(void)
{
    draw_panel(SPEED_PANEL_X, SPEED_PANEL_Y,
               SPEED_PANEL_W, SPEED_PANEL_H, NULL);

    draw_panel(SPEED_PANEL_X + 1, SPEED_PANEL_Y + 1,
               SPEED_PANEL_W - 2, 14, get_string(MSG_SPEED_COMPARISONS));

    draw_speed_panel_contents(TRUE);
}

/* Refresh just the changing date/time rows, including midnight rollover. */

void refresh_clock_page(void)
{
    char values[2][24];
    WORD i, n;
    if (app->current_view != VIEW_MAIN ||
        app->hardware_type != HARDWARE_CLOCK || overlay_backup.valid)
        return;
    format_clock_values(values);
    for (i = 0; i < hardware_row_count; i++) {
        HardwareRow *row = &hardware_rows[i];
        for (n = 0; n < 2; n++) {
            if (row->label == get_string(n ? MSG_RTC_TIME : MSG_RTC_DATE) &&
                row->page == hardware_page && strcmp(row->value, values[n])) {
                copy_string(row->value, values[n], sizeof(row->value));
                draw_hardware_row(row, TRUE);
            }
        }
    }
}

/*
 * Draw hardware panel
 */

static void draw_hardware_panel(void)
{
    draw_panel(HARDWARE_PANEL_X, HARDWARE_PANEL_Y,
               HARDWARE_PANEL_W, HARDWARE_PANEL_H,
           NULL);

    draw_panel(HARDWARE_PANEL_X + 1, HARDWARE_PANEL_Y + 1,
               HARDWARE_PANEL_W - 2, 14,
           get_string(MSG_INTERNAL_HARDWARE));

    Button *hw_cycle_btn = find_button(BTN_HARDWARE_CYCLE);
    if (hw_cycle_btn) {
        draw_cycle_button(hw_cycle_btn);
    }

    draw_hardware_panel_contents();
}

/* Rows own their values because most callers reuse a formatting buffer. */
static void collect_hardware_row(const HardwareInfoRow *info, void *data)
{
    HardwareRow *row;
    (void)data;
    if (hardware_row_count >= MAX_HARDWARE_ROWS) return;
    row = &hardware_rows[hardware_row_count++];
    memset(row, 0, sizeof(*row));
    row->label = info->label;
    row->has_value = info->value != NULL;
    copy_string(row->value, info->value ? info->value : "", sizeof(row->value));
    row->indent = info->detail ? 14 : 0;
    row->offset = info->detail ?
        (info->label == get_string(MSG_CHIP_ID) ? 76 : 110) :
        !info->value ? 120 :
        app->hardware_type == HARDWARE_CPU ? 80 :
        app->hardware_type == HARDWARE_SCSI ? HARDWARE_CHIPSET_VALUE_OFFSET :
        HARDWARE_OVERVIEW_VALUE_OFFSET;
    row->height = app->rp->TxHeight;
    row->group = info->group;
    if (info->control != CACHE_NONE) {
        row->control = BTN_ICACHE + info->control - CACHE_ICACHE;
        row->height += 3;
        copy_string(row->value, get_string(cache_setting_enabled(info->control) ?
                    MSG_BTN_ON : MSG_BTN_OFF), sizeof(row->value));
    }
}

static void build_hardware_rows(void)
{
    hardware_row_count = 0;
    visit_hardware_rows(app->hardware_type, collect_hardware_row, NULL);
    layout_hardware_rows();
}

/*
 * Draw bottom buttons
 */
static void draw_bottom_buttons(void)
{
    int i;
    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_QUIT && buttons[i].id <= BTN_REPORT) {
            draw_button(&buttons[i]);
        }
    }
}

/*
 * Draw inline cache toggle buttons (in hardware panel right column)
 */
static void draw_cache_buttons(void)
{
    int i;

    if (app->hardware_type != HARDWARE_CPU)
        return;

    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_ICACHE && buttons[i].id <= BTN_SUPER_SCALAR) {
            draw_button(&buttons[i]);
        }
    }
}

/*
 * Refresh all cache button labels and states after any cache toggle
 * Re-reads actual cache state from hardware and updates all buttons
 */
static void refresh_all_cache_buttons(void)
{
    refresh_cache_status();
    update_hardware_text();
}

/*
 * Handle mouse click, return button ID if hit
 */
ButtonID handle_click(WORD mx, WORD my)
{
    int i;

    for (i = 0; i < num_buttons; i++) {
        Button *btn = &buttons[i];
        if (btn->enabled &&
            mx >= btn->x && mx < btn->x + btn->width &&
            my >= btn->y && my < btn->y + btn->height) {
            return btn->id;
        }
    }

    return BTN_NONE;
}

/*
 * Handle button press action
 */
void handle_button_press(ButtonID btn_id)
{
    switch (app->current_view) {
        case VIEW_MAIN:
            main_view_handle_button(btn_id);
            break;

        case VIEW_MEMORY:
            memory_view_handle_button(btn_id);
            break;

        case VIEW_DRIVES:
            drives_view_handle_button(btn_id);
            break;

        case VIEW_BOARDS:
            boards_view_handle_button(btn_id);
            break;

        case VIEW_BOARD_DETAILS:
            board_detail_view_handle_button(btn_id);
            break;

        case VIEW_REPORT:
            report_view_handle_button(btn_id);
            break;
        case VIEW_SCSI:
            scsi_view_handle_button(btn_id);
            break;
    }
}

/*
 * Page on trough clicks; drag only when the knob itself was grabbed.
 */
void handle_scrollbar_click(WORD mx __attribute__((unused)), WORD my)
{
    Button *scrollbar_btn = find_button(BTN_SOFTWARE_SCROLLBAR);
    SoftwareList *list = get_software_list(app->software_type);
    WORD knob_y, knob_h, travel;
    LONG max_scroll, new_scroll;

    if (app->current_view == VIEW_REPORT) {
        report_view_scrollbar(my);
        return;
    }

    if (app->current_view != VIEW_MAIN || !scrollbar_btn ||
        !scrollbar_btn->enabled || !list) return;

    max_scroll = (LONG)list->count - SOFTWARE_LIST_LINES;
    if (max_scroll <= 0) return;

    scrollbar_knob(scrollbar_btn->height, app->software_scroll, list->count,
                   SOFTWARE_LIST_LINES, &knob_y, &knob_h);
    travel = scrollbar_btn->height - 2 - knob_h;
    if (travel <= 0) return;

    if (app->scrollbar_dragging) {
        LONG delta = (LONG)my - app->scrollbar_drag_y;
        LONG start_y = (app->scrollbar_drag_scroll * travel) / max_scroll;

        /* Retain the original scroll offset even when several rows share a
         * knob pixel. A click or horizontal movement must not move the list. */
        new_scroll = app->scrollbar_drag_scroll + (delta * max_scroll) / travel;
        if (delta < 0 && start_y + delta <= 0) new_scroll = 0;
        if (delta > 0 && start_y + delta >= travel) new_scroll = max_scroll;
    } else {
        knob_y += scrollbar_btn->y;
        if (my < knob_y) {
            new_scroll = app->software_scroll - SOFTWARE_LIST_LINES;
        } else if (my >= knob_y + knob_h) {
            new_scroll = app->software_scroll + SOFTWARE_LIST_LINES;
        } else {
            app->scrollbar_dragging = TRUE;
            app->scrollbar_drag_y = my;
            app->scrollbar_drag_scroll = app->software_scroll;
            return;
        }
    }

    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    if (new_scroll != app->software_scroll) {
        app->software_scroll = new_scroll;
        update_software_list(FALSE);
    }
}

/*
 * Switch to a different view
 */
void switch_to_view(ViewMode view)
{
    ViewMode previous = app->current_view;
    if (previous == VIEW_REPORT && view != VIEW_REPORT) close_report_view();
    app->scrollbar_dragging = FALSE;
    app->current_view = view;

    /* Reset view-specific state */
    switch (view) {
        case VIEW_MEMORY:
            app->memory_region_index = 0;
            break;
        case VIEW_DRIVES:
            if (drive_list.count == 0) {
                app->selected_drive = -1;
            } else if (app->selected_drive < 0 ||
                       app->selected_drive >= (LONG)drive_list.count) {
                app->selected_drive = 0;
            }
            break;
        case VIEW_BOARDS:
            if (previous != VIEW_BOARD_DETAILS)
                app->board_scroll = 0;
            break;
        default:
            break;
    }

    redraw_current_view();
}

/* Position/control, one transparent row, and the sprite terminator. */
#define BLANK_POINTER_SIZE (6 * sizeof(UWORD))
static UWORD *blank_pointer;

static void free_overlay_backup(void)
{
    UBYTE plane;

    WaitBlit();

    if (overlay_backup.allocated_bitmap && overlay_backup.bitmap) {
        FreeBitMap(overlay_backup.bitmap);
    } else {
        for (plane = 0; plane < overlay_backup.depth && plane < 8; plane++) {
            if (overlay_backup.legacy_bitmap.Planes[plane]) {
                FreeRaster(overlay_backup.legacy_bitmap.Planes[plane],
                           overlay_backup.w, overlay_backup.h);
                overlay_backup.legacy_bitmap.Planes[plane] = NULL;
            }
        }
    }

    overlay_backup.bitmap = NULL;
    overlay_backup.valid = FALSE;
    overlay_backup.depth = 0;
    overlay_backup.allocated_bitmap = FALSE;
}

static BOOL save_overlay_area(WORD x, WORD y, WORD w, WORD h)
{
    struct RastPort backup_rp;
    ULONG depth;
    UBYTE plane;

    if (!app->rp || !app->rp->BitMap || w <= 0 || h <= 0)
        return FALSE;

    if (GfxBase->LibNode.lib_Version >= 39) {
        depth = GetBitMapAttr(app->rp->BitMap, BMA_DEPTH);
    } else {
        depth = app->rp->BitMap->Depth;
    }
    if (depth == 0 || depth > 255)
        return FALSE;

    if (overlay_backup.valid)
        free_overlay_backup();

    memset(&overlay_backup.legacy_bitmap, 0, sizeof(overlay_backup.legacy_bitmap));

    overlay_backup.x = x;
    overlay_backup.y = y;
    overlay_backup.w = w;
    overlay_backup.h = h;
    overlay_backup.depth = (UBYTE)depth;
    overlay_backup.bitmap = NULL;
    overlay_backup.allocated_bitmap = FALSE;

    if (GfxBase->LibNode.lib_Version >= 39) {
        ULONG flags = 0;

        if (app->use_custom_screen)
            flags |= BMF_STANDARD;

        overlay_backup.bitmap = AllocBitMap(w, h, depth, flags,
                                            app->rp->BitMap);
        if (!overlay_backup.bitmap)
            return FALSE;
        overlay_backup.allocated_bitmap = TRUE;
    } else {
        if (depth > 8)
            return FALSE;

        InitBitMap(&overlay_backup.legacy_bitmap, depth, w, h);
        overlay_backup.bitmap = &overlay_backup.legacy_bitmap;
        for (plane = 0; plane < depth; plane++) {
            overlay_backup.legacy_bitmap.Planes[plane] = AllocRaster(w, h);
            if (!overlay_backup.legacy_bitmap.Planes[plane]) {
                free_overlay_backup();
                return FALSE;
            }
        }
    }

    InitRastPort(&backup_rp);
    backup_rp.BitMap = overlay_backup.bitmap;

    WaitBlit();
    SetRast(&backup_rp, 0);
    WaitBlit();
    ClipBlit(app->rp, x, y, &backup_rp, 0, 0, w, h,
             OVERLAY_COPY_MINTERM);
    WaitBlit();
    overlay_backup.valid = TRUE;

    return TRUE;
}

static BOOL restore_overlay_area(void)
{
    struct RastPort backup_rp;

    if (!overlay_backup.valid)
        return FALSE;

    InitRastPort(&backup_rp);
    backup_rp.BitMap = overlay_backup.bitmap;

    ClipBlit(&backup_rp, 0, 0, app->rp,
             overlay_backup.x, overlay_backup.y,
             overlay_backup.w, overlay_backup.h, OVERLAY_COPY_MINTERM);
    WaitBlit();
    free_overlay_backup();

    return TRUE;
}

static void show_status_overlay_centered(const char *message,
                                         WORD area_x, WORD area_y,
                                         WORD area_w, WORD area_h)
{
    struct RastPort *rp = app->rp;
    WORD text_len = strlen(message);

    /* Dialog dimensions and position (centered) */
    WORD text_width = TextLength(rp, (CONST_STRPTR)message, text_len);
    WORD dialog_w = text_width + 32;
    WORD dialog_h = 28;
    WORD dialog_x = area_x + (area_w - dialog_w) / 2;
    WORD dialog_y = area_y + (area_h - dialog_h) / 2;

    if (!save_overlay_area(dialog_x, dialog_y, dialog_w, dialog_h))
        return;

    /* Allocate explicitly: some toolchains lose __chip hunk flags. */
    if (!blank_pointer)
        blank_pointer = AllocMem(BLANK_POINTER_SIZE, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer)
        SetPointer(app->window, blank_pointer, 1, 16, 0, 0);

    /* Draw red background */
    SetAPen(rp, COLOR_BAR_YOU);  /* Red color */
    RectFill(rp, dialog_x, dialog_y, dialog_x + dialog_w - 1, dialog_y + dialog_h - 1);

    /* Draw 3D border */
    draw_3d_box(dialog_x, dialog_y, dialog_w, dialog_h, FALSE);

    /* Draw centered message */
    SetAPen(rp, COLOR_HIGHLIGHT);  /* White text in both palettes */
    SetBPen(rp, COLOR_BAR_YOU);
    TextLength(rp, (CONST_STRPTR)message, text_len);
    Move(rp, dialog_x + (dialog_w - text_width) / 2, dialog_y + 16);
    Text(rp, (CONST_STRPTR)message, text_len);
}

/*
 * Show status overlay (red background, centered message, no interaction)
 */
void show_status_overlay(const char *message)
{
    show_status_overlay_centered(message, 0, 0,
                                 SCREEN_WIDTH, app->screen_height);
}

static void show_speed_status_overlay(const char *message)
{
    show_status_overlay_centered(message,
                                 SPEED_PANEL_X, SPEED_PANEL_Y,
                                 SPEED_PANEL_W, SPEED_PANEL_H);
}

/*
 * Hide status overlay and restore view
 */
void hide_status_overlay(void)
{
    if (blank_pointer) {
        ClearPointer(app->window);
        /* Let the copper switch pointers before releasing sprite data. */
        WaitTOF();
        FreeMem(blank_pointer, BLANK_POINTER_SIZE);
        blank_pointer = NULL;
    }

    restore_overlay_area();
}

/*
 * Draw just the text field contents (for fast updates while typing)
 */
static void draw_requester_field(WORD field_x, WORD field_y, WORD field_w, WORD field_h,
                                 const char *filename, ULONG cursor_pos)
{
    struct RastPort *rp = app->rp;
    WORD cursor_x, cursor_w;
    WORD max_text_w;

    /* Clear field interior */
    SetAPen(rp, COLOR_BACKGROUND);
    RectFill(rp, field_x + 2, field_y + 2,
             field_x + field_w - 3, field_y + field_h - 3);

    /* Draw filename text */
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_BACKGROUND);
    max_text_w = field_w - 8;
    draw_text_clipped(field_x + 4, field_y + 10, filename, max_text_w);

    /* Draw cursor */
    cursor_x = field_x + 4;
    if (cursor_pos > 0) {
        cursor_x += TextLength(rp, (CONST_STRPTR)filename, cursor_pos);
    }
    cursor_w = TextLength(rp, filename[cursor_pos] ?
                          (CONST_STRPTR)&filename[cursor_pos] :
                          (CONST_STRPTR)" ", 1);
    if (cursor_w < 1) cursor_w = 1;
    if (cursor_x > field_x + field_w - cursor_w - 2) {
        cursor_x = field_x + field_w - cursor_w - 2;
    }
    SetAPen(rp, COLOR_TEXT);
    RectFill(rp, cursor_x, field_y + 2,
             cursor_x + cursor_w - 1, field_y + field_h - 3);
    /* Draw character at cursor position in inverse */
    if (filename[cursor_pos]) {
        SetAPen(rp, COLOR_BACKGROUND);
        SetBPen(rp, COLOR_TEXT);
        TextLength(rp, (CONST_STRPTR)&filename[cursor_pos], 1);
        Move(rp, cursor_x, field_y + 10);
        Text(rp, (CONST_STRPTR)&filename[cursor_pos], 1);
    }
}

static void draw_requester_text_centered(WORD x, WORD y, WORD width,
                                         const char *text)
{
    struct RastPort *rp = app->rp;
    WORD text_width = TextLength(rp, (CONST_STRPTR)text, strlen(text));
    WORD text_x = x + (width - text_width) / 2;

    if (text_x < x + 2) {
        text_x = x + 2;
    }
    draw_text_clipped(text_x, y, text, width - 4);
}

/*
 * Draw overlay requester dialog (full redraw)
 */
static void draw_requester_overlay(WORD x, WORD y, WORD w, WORD h,
                                   const char *title, const char *filename,
                                   ULONG cursor_pos)
{
    struct RastPort *rp = app->rp;
    WORD field_x, field_y, field_w, field_h;
    WORD btn_y, btn_w, btn_h;

    /* Draw outer panel with shadow effect */
    //SetAPen(rp, COLOR_BUTTON_DARK);
    //RectFill(rp, x + 2, y + 2, x + w + 1, y + h + 1);

    /* Draw main panel background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x, y, x + w - 1, y + h - 1);

    /* Draw 3D border */
    draw_3d_box(x, y, w, h, FALSE);

    /* Draw title bar */
    SetAPen(rp, COLOR_BUTTON_DARK);
    RectFill(rp, x + 2, y + 2, x + w - 3, y + 14);
    SetAPen(rp, COLOR_BUTTON_LIGHT);
    SetBPen(rp, COLOR_BUTTON_DARK);
    draw_requester_text_centered(x, y + 11, w, title);

    /* Draw filename input field border */
    field_x = x + 16;
    field_y = y + 24;
    field_w = w - 32;
    field_h = 14;

    /* Recessed field background */
    SetAPen(rp, COLOR_BACKGROUND);
    RectFill(rp, field_x, field_y, field_x + field_w - 1, field_y + field_h - 1);
    draw_3d_box(field_x, field_y, field_w, field_h, TRUE);

    /* Draw field contents */
    draw_requester_field(field_x, field_y, field_w, field_h, filename, cursor_pos);

    /* Draw OK and CANCEL buttons */
    btn_y = y + h - 20;
    btn_w = 80;
    btn_h = 14;

    /* OK button */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x + 24, btn_y, x + 24 + btn_w - 1, btn_y + btn_h - 1);
    draw_3d_box(x + 24, btn_y, btn_w, btn_h, FALSE);
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_requester_text_centered(x + 24, btn_y + 10, btn_w,
                                 get_string(MSG_BTN_OK));

    /* CANCEL button */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x + w - 24 - btn_w, btn_y, x + w - 24 - 1, btn_y + btn_h - 1);
    draw_3d_box(x + w - 24 - btn_w, btn_y, btn_w, btn_h, FALSE);
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_requester_text_centered(x + w - 24 - btn_w, btn_y + 10, btn_w,
                                 get_string(MSG_BTN_CANCEL));
}

/*
 * Show filename requester overlay
 * Returns TRUE if OK was pressed, FALSE if cancelled
 * filename buffer is modified with the entered filename
 */
BOOL show_filename_requester(const char *title, char *filename, ULONG filename_size)
{
    struct IntuiMessage *msg;
    BOOL running = TRUE;
    BOOL result = FALSE;
    ULONG cursor_pos;
    ULONG filename_len;
    Button *pressed_btn = NULL;

    /* Dialog dimensions and position (centered) */
    WORD dialog_w = 320;
    WORD dialog_h = 60;
    WORD dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
    WORD dialog_y = (app->screen_height - dialog_h) / 2;

    /* Field position (must match draw_requester_overlay) */
    WORD field_x = dialog_x + 16;
    WORD field_y = dialog_y + 24;
    WORD field_w = dialog_w - 32;
    WORD field_h = 14;

    /* Button positions */
    WORD btn_y = dialog_y + dialog_h - 20;
    WORD btn_w = 80;
    WORD btn_h = 14;
    WORD ok_x = dialog_x + 24;
    WORD cancel_x = dialog_x + dialog_w - 24 - btn_w;

    /* Button structs for OK and CANCEL */
    Button ok_btn = { ok_x, btn_y, btn_w, btn_h, get_string(MSG_BTN_OK), BTN_NONE, TRUE, FALSE };
    Button cancel_btn = { cancel_x, btn_y, btn_w, btn_h, get_string(MSG_BTN_CANCEL), BTN_NONE, TRUE, FALSE };

    if (!save_overlay_area(dialog_x, dialog_y, dialog_w, dialog_h))
        return FALSE;

    /* Initialize cursor position at end of filename */
    filename_len = strlen(filename);
    cursor_pos = filename_len;

    /* Draw initial dialog */
    draw_requester_overlay(dialog_x, dialog_y, dialog_w, dialog_h,
                           title, filename, cursor_pos);

    /* Event loop for dialog */
    while (running) {
        WaitPort(app->window->UserPort);

        while ((msg = (struct IntuiMessage *)
                GetMsg(app->window->UserPort)) != NULL) {

            ULONG class = msg->Class;
            UWORD code = msg->Code;
            WORD mx = msg->MouseX;
            WORD my = msg->MouseY;

            if (!app->use_custom_screen) {
                mx -= app->window->BorderLeft;
                my -= app->window->BorderTop;
            }

            ReplyMsg((struct Message *)msg);

            if (!running) {
                continue;
            }

            switch (class) {
                case IDCMP_MOUSEBUTTONS:
                    if (code == SELECTDOWN) {
                        /* Check OK button */
                        if (mx >= ok_x && mx < ok_x + btn_w &&
                            my >= btn_y && my < btn_y + btn_h) {
                            pressed_btn = &ok_btn;
                            ok_btn.pressed = TRUE;
                            draw_button(&ok_btn);
                        }
                        /* Check CANCEL button */
                        else if (mx >= cancel_x && mx < cancel_x + btn_w &&
                                 my >= btn_y && my < btn_y + btn_h) {
                            pressed_btn = &cancel_btn;
                            cancel_btn.pressed = TRUE;
                            draw_button(&cancel_btn);
                        }
                    } else if (code == SELECTUP && pressed_btn) {
                        /* Release the button */
                        pressed_btn->pressed = FALSE;
                        draw_button(pressed_btn);
                        /* Check if still over the same button */
                        if (pressed_btn == &ok_btn &&
                            mx >= ok_x && mx < ok_x + btn_w &&
                            my >= btn_y && my < btn_y + btn_h) {
                            result = TRUE;
                            running = FALSE;
                        } else if (pressed_btn == &cancel_btn &&
                                   mx >= cancel_x && mx < cancel_x + btn_w &&
                                   my >= btn_y && my < btn_y + btn_h) {
                            result = FALSE;
                            running = FALSE;
                        }
                        pressed_btn = NULL;
                    }
                    break;

                case IDCMP_VANILLAKEY:
                    if (code == 0x0D) {  /* Return/Enter */
                        result = TRUE;
                        running = FALSE;
                    } else if (code == 0x1B) {  /* Escape */
                        result = FALSE;
                        running = FALSE;
                    } else if (code == 0x08) {  /* Backspace */
                        if (cursor_pos > 0) {
                            /* Remove character before cursor */
                            memmove(&filename[cursor_pos - 1],
                                    &filename[cursor_pos],
                                    filename_len - cursor_pos + 1);
                            cursor_pos--;
                            filename_len--;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    } else if (code == 0x7F) {  /* Delete */
                        if (cursor_pos < filename_len) {
                            memmove(&filename[cursor_pos],
                                    &filename[cursor_pos + 1],
                                    filename_len - cursor_pos);
                            filename_len--;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    } else if (code >= 32 && code < 127) {  /* Printable character */
                        if (filename_len < filename_size - 1) {
                            /* Insert character at cursor */
                            memmove(&filename[cursor_pos + 1],
                                    &filename[cursor_pos],
                                    filename_len - cursor_pos + 1);
                            filename[cursor_pos] = (char)code;
                            cursor_pos++;
                            filename_len++;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    }
                    break;

                case IDCMP_RAWKEY:
                    /* Ignore key up events (bit 7 set) */
                    if (code & IECODE_UP_PREFIX) break;

                    /* Handle cursor keys and delete */
                    if (code == CURSORLEFT) {
                        if (cursor_pos > 0) {
                            cursor_pos--;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    } else if (code == CURSORRIGHT) {
                        if (cursor_pos < filename_len) {
                            cursor_pos++;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    } else if (code == 0x46) {  /* Delete key */
                        if (cursor_pos < filename_len) {
                            memmove(&filename[cursor_pos],
                                    &filename[cursor_pos + 1],
                                    filename_len - cursor_pos);
                            filename_len--;
                            draw_requester_field(field_x, field_y, field_w, field_h,
                                                 filename, cursor_pos);
                        }
                    }
                    break;
            }
        }
    }

    restore_overlay_area();

    return result;
}
