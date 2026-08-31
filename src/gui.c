// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - GUI rendering and event handling
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
#include "hardware.h"
#include "benchmark.h"
#include "software.h"
#include "memory.h"
#include "drives.h"
#include "boards.h"
#include "scsi.h"
#include "print.h"
#include "cache.h"
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

/* Static buffers for cache button labels (name + ON/OFF/N/A status) */
static char icache_label[16], dcache_label[16], iburst_label[16];
static char dburst_label[16], cback_label[16], super_scalar_label[16];

#define CACHE_BTN_X (HARDWARE_PANEL_X + 226)
#define CACHE_BTN_W 32
#define CACHE_BTN_H 11
#define CACHE_ROW_Y0 (HARDWARE_PANEL_Y + 84)
#define CACHE_LABEL_Y0 (CACHE_ROW_Y0 + 8)
#define CACHE_ROW_STEP 11
#define HARDWARE_OVERVIEW_VALUE_OFFSET 90
#define HARDWARE_CHIPSET_VALUE_OFFSET 124

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
static void draw_bottom_buttons(void);
static void draw_cache_buttons(void);
static void clear_buttons(void);
static void update_software_list(void);
static void update_hardware_text(void);
static void refresh_all_cache_buttons(void);
static void show_timed_overlay(const char *message, ULONG ticks);
static void show_status_overlay_centered(const char *message,
                                         WORD area_x, WORD area_y,
                                         WORD area_w, WORD area_h);
static void show_speed_status_overlay(const char *message);
static const char *get_hardware_page_label(void);
static void update_cache_button_enabled_states(void);
static void refresh_hardware_benchmark_rows(void);
static void format_cpu_value(char *buffer, size_t size);
static void format_fpu_value(char *buffer, size_t size);
static void format_mmu_value(char *buffer, size_t size);
static void format_mmu_address(char *buffer, size_t size, ULONG address);

void format_scaled(char *buffer, size_t size, ULONG value_x100, BOOL round)
{
    ULONG integer_part = value_x100 / 100;
    ULONG frac_part = value_x100 % 100;
    if (round && integer_part >= 100) {
        /* Round up if fractional part >= 0.5 */
        if (frac_part >= 50) {
            integer_part++;
        }
        snprintf(buffer, size, "%lu", (unsigned long)integer_part);
    } else {
        snprintf(buffer, size, "%lu.%02lu",
                 (unsigned long)integer_part,
                 (unsigned long)frac_part);
    }
}

void TightText(struct RastPort *rp, int x, int y, CONST_STRPTR str, int charGap, int spaceWidth)
{
    int currentX = x;
    int targetWidth = 0;

    /*
     * Negative gaps were tuned around Topaz 8. Treat them as target
     * advances from that font and leave already-narrow fonts alone.
     */
    if (charGap < 0)
        targetWidth = 8 + charGap;

    Move(rp, x, y);

    for (int i = 0; str[i]; i++) {
        if (str[i] == ' ') {
            currentX += spaceWidth;
        } else {
            int charWidth = TextLength(rp, &str[i], 1);
            int effectiveGap = charGap;

            if (targetWidth > 0) {
                if (charWidth <= targetWidth)
                    effectiveGap = 0;
                else
                    effectiveGap = targetWidth - charWidth;
            }

            Move(rp, currentX, y);
            Text(rp, &str[i], 1);
            currentX += charWidth + effectiveGap;
        }
    }
}

/*
 * Initialize buttons for current view
 */
void init_buttons(void)
{
    clear_buttons();
    update_button_states();
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

static const char *get_hardware_page_label(void)
{
    switch (app->hardware_type) {
        case HARDWARE_CPU:
            return get_string(MSG_HARDWARE_CPU);
        case HARDWARE_EXT:
            return get_string(MSG_HARDWARE_EXT);
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

static void update_cache_button_enabled_states(void)
{
    BOOL cpu_page = app->hardware_type == HARDWARE_CPU;

    set_button_enabled(BTN_ICACHE, cpu_page && hw_info.has_icache);
    set_button_enabled(BTN_DCACHE, cpu_page && hw_info.has_dcache);
    set_button_enabled(BTN_IBURST, cpu_page && hw_info.has_iburst);
    set_button_enabled(BTN_DBURST, cpu_page && hw_info.has_dburst);
    set_button_enabled(BTN_CBACK, cpu_page && hw_info.has_copyback);
    set_button_enabled(BTN_SUPER_SCALAR,
                       cpu_page && hw_info.has_super_scalar);
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
    if (id == BTN_SOFTWARE_UP) {
        draw_scroll_arrow(btn->x, btn->y, btn->width, btn->height,
                          TRUE, btn->pressed);
    } else if (id == BTN_SOFTWARE_DOWN) {
        draw_scroll_arrow(btn->x, btn->y, btn->width, btn->height,
                          FALSE, btn->pressed);
    } else if (id == BTN_SOFTWARE_CYCLE || id == BTN_SCALE_TOGGLE || id == BTN_HARDWARE_CYCLE) {
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
               get_string(MSG_BTN_PRINT), BTN_PRINT, TRUE);

    /* Software type cycle button */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 98,
               SOFTWARE_PANEL_Y + 2, 92, 12,
               app->software_type == SOFTWARE_LIBRARIES ?
                   get_string(MSG_LIBRARIES) :
               app->software_type == SOFTWARE_DEVICES ?
                   get_string(MSG_DEVICES) :
               app->software_type == SOFTWARE_RESOURCES ?
                   get_string(MSG_RESOURCES):
                   get_string(MSG_MMU_ENTRIES),
               BTN_SOFTWARE_CYCLE, TRUE);

    /* Software scroll buttons (arrows on right side) */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + 15, 12, 10,
               NULL, BTN_SOFTWARE_UP, TRUE);   /* Up arrow */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + 15 + 10, 12, SOFTWARE_PANEL_H - 15 - 10 - 12,
               NULL, BTN_SOFTWARE_SCROLLBAR, TRUE);  /* Scroll bar */
    add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
               SOFTWARE_PANEL_Y + SOFTWARE_PANEL_H - 12, 12, 10,
               NULL, BTN_SOFTWARE_DOWN, TRUE); /* Down arrow */

    /* Scale toggle button */
    add_button(SPEED_PANEL_X + SPEED_PANEL_W - 68,
               SPEED_PANEL_Y + 2, 64, 12,
               app->bar_scale == SCALE_SHRINK ?
                   get_string(MSG_SHRINK) : get_string(MSG_EXPAND),
               BTN_SCALE_TOGGLE, TRUE);

     /* Hardware type cycle button */
    add_button(HARDWARE_PANEL_X + HARDWARE_PANEL_W - 84,
               HARDWARE_PANEL_Y + 2, 82, 12,
               get_hardware_page_label(),
               BTN_HARDWARE_CYCLE, TRUE);

    if (app->hardware_type == HARDWARE_CPU) {
        /* Inline cache toggle buttons in hardware panel. */
        snprintf(icache_label, sizeof(icache_label), "%s",
                 hw_info.has_icache ?
                     (hw_info.icache_enabled ? get_string(MSG_ON) :
                                               get_string(MSG_OFF)) :
                     get_string(MSG_NA));
        snprintf(dcache_label, sizeof(dcache_label), "%s",
                 hw_info.has_dcache ?
                     (hw_info.dcache_enabled ? get_string(MSG_ON) :
                                               get_string(MSG_OFF)) :
                     get_string(MSG_NA));
        snprintf(iburst_label, sizeof(iburst_label), "%s",
                 hw_info.has_iburst ?
                     (hw_info.iburst_enabled ? get_string(MSG_ON) :
                                               get_string(MSG_OFF)) :
                     get_string(MSG_NA));
        snprintf(dburst_label, sizeof(dburst_label), "%s",
                 hw_info.has_dburst ?
                     (hw_info.dburst_enabled ? get_string(MSG_ON) :
                                               get_string(MSG_OFF)) :
                     get_string(MSG_NA));
        snprintf(cback_label, sizeof(cback_label), "%s",
                 hw_info.has_copyback ?
                     (hw_info.copyback_enabled ? get_string(MSG_ON) :
                                                 get_string(MSG_OFF)) :
                     get_string(MSG_NA));
        snprintf(super_scalar_label, sizeof(super_scalar_label), "%s",
                 hw_info.has_super_scalar ?
                     (hw_info.super_scalar_enabled ? get_string(MSG_ON) :
                                                     get_string(MSG_OFF)) :
                     get_string(MSG_NA));

        add_button(CACHE_BTN_X, CACHE_ROW_Y0, CACHE_BTN_W, CACHE_BTN_H,
                   icache_label, BTN_ICACHE, FALSE);
        add_button(CACHE_BTN_X, CACHE_ROW_Y0 + CACHE_ROW_STEP, CACHE_BTN_W,
                   CACHE_BTN_H, dcache_label, BTN_DCACHE, FALSE);
        add_button(CACHE_BTN_X, CACHE_ROW_Y0 + CACHE_ROW_STEP * 2,
                   CACHE_BTN_W, CACHE_BTN_H, iburst_label, BTN_IBURST, FALSE);
        add_button(CACHE_BTN_X, CACHE_ROW_Y0 + CACHE_ROW_STEP * 3,
                   CACHE_BTN_W, CACHE_BTN_H, dburst_label, BTN_DBURST,
                   FALSE);
        add_button(CACHE_BTN_X, CACHE_ROW_Y0 + CACHE_ROW_STEP * 4,
                   CACHE_BTN_W, CACHE_BTN_H, cback_label, BTN_CBACK, FALSE);
        add_button(CACHE_BTN_X, CACHE_ROW_Y0 + CACHE_ROW_STEP * 5,
                   CACHE_BTN_W, CACHE_BTN_H, super_scalar_label,
                   BTN_SUPER_SCALAR, FALSE);
        update_cache_button_enabled_states();

        set_button_pressed(BTN_ICACHE, hw_info.icache_enabled);
        set_button_pressed(BTN_DCACHE, hw_info.dcache_enabled);
        set_button_pressed(BTN_IBURST, hw_info.iburst_enabled);
        set_button_pressed(BTN_DBURST, hw_info.dburst_enabled);
        set_button_pressed(BTN_CBACK, hw_info.copyback_enabled);
        set_button_pressed(BTN_SUPER_SCALAR, hw_info.super_scalar_enabled);
    }
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

        case BTN_PRINT:
            {
                char filename[MAX_FILENAME_LEN];
                strncpy(filename, DEFAULT_OUTPUT_FILE, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';

                if (show_filename_requester(
                        get_string(MSG_ENTER_FILENAME), filename, sizeof(filename))) {
                    if (!export_to_file(filename)) {
                        show_timed_overlay("Export failed", 100);
                    }
                }
            }
            break;

        case BTN_SOFTWARE_CYCLE:
            app->software_type = (app->software_type + 1) % 4;
            app->software_scroll = 0;
            update_software_list();
            break;
        case BTN_HARDWARE_CYCLE:
            app->hardware_type =
                (app->hardware_type + 1) % HARDWARE_COUNT;
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
                update_software_list();
            }
            break;

        case BTN_SOFTWARE_DOWN:
            {
                SoftwareList *list = app->software_type == SOFTWARE_LIBRARIES ?
                                         &libraries_list :
                                     app->software_type == SOFTWARE_DEVICES ?
                                         &devices_list :
                                     app->software_type == SOFTWARE_RESOURCES ?
                                         &resources_list : &mmu_list;
                if (app->software_scroll < (LONG)list->count - SOFTWARE_LIST_LINES) {
                    app->software_scroll++;
                    update_software_list();
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

static BOOL xsysinfo_logo_pixel(WORD x, WORD y)
{
    return (xsysinfo_logo_template[y][x >> 4] & (0x8000 >> (x & 15))) != 0;
}

static BOOL decorative_dots_available(void)
{
    return app->screen && app->screen->BitMap.Depth >= 3;
}

static void draw_dotted_row(WORD x, WORD y, WORD max_x, UWORD pattern)
{
    struct RastPort *rp = app->rp;

    if (max_x <= x)
        return;

    SetAPen(rp, COLOR_BACKGROUND);
    SetDrMd(rp, JAM1);
    SetDrPt(rp, pattern);
    Move(rp, x, y);
    Draw(rp, max_x - 1, y);
    SetDrPt(rp, 0xffff);
}


/* A somewhat fast dithered gradient fill using a patterned RectFill() with 257 pre-generated dither levels. */

#include "bayer-16x16.c"

void draw_gradient(WORD left, WORD top, WORD width, WORD height, WORD start_color, WORD end_color)
{
	struct RastPort *rp = app->rp;

	SetDrMd(rp, JAM2);

	SetBPen(rp, start_color);
	SetAPen(rp, end_color);

	WORD right = left+width;
	WORD bottom = top+height;

	// We fill in 16-pixel-wide vertical bands so we can use RectFill().
	// Currently the fill patterns are 16x16. Taller might be falter for larger regions.

    for (WORD x = left; x < right; x+=16) {
		// The final band could be narrower than 16 pixels.
		WORD fill_width = right - x;
		if (fill_width > 16) {
			fill_width = 16;
		}

		WORD level = ((x-left) * 257) / (width - 1);
		SetAfPt(rp, bayer16x16[level], 4);	// 4 -> 2^4 = 16 rows in each dither pattern
		RectFill(rp, x, top, x+fill_width-1, bottom);
	}

	SetAfPt(rp, NULL, 0);	// Restore solid fill pattern
}

/* Convenience wrapper for drawing a three-colour gradient (two gradients meeting in the middle) */
void draw_gradient_3(WORD left, WORD top, WORD width, WORD height, WORD start_color, WORD middle_color, WORD end_color) {
	draw_gradient(left, top, width/2, height, start_color, middle_color);
	draw_gradient(left+width/2, top, width/2, height, middle_color, end_color);
}


static WORD shadow_text_color(void)
{
    return app->dark_mode ? COLOR_BACKGROUND : COLOR_TEXT;
}

static void draw_xsysinfo_logo_mask(struct RastPort *rp, WORD x, WORD y, WORD color)
{
    WORD row, col, start;

    SetAPen(rp, color);
    for (row = 0; row < XSYSINFO_LOGO_H; row++) {
        start = -1;
        for (col = 0; col <= XSYSINFO_LOGO_W; col++) {
            BOOL on = (col < XSYSINFO_LOGO_W) && xsysinfo_logo_pixel(col, row);

            if (on && start < 0) {
                start = col;
            } else if (!on && start >= 0) {
                if (start == col - 1) {
                    WritePixel(rp, x + start, y + row);
                } else {
                    Move(rp, x + start, y + row);
                    Draw(rp, x + col - 1, y + row);
                }
                start = -1;
            }
        }
    }
}

static void draw_xsysinfo_logo(WORD x, WORD y)
{
    struct RastPort *rp = app->rp;

    SetDrMd(rp, JAM1);
    draw_xsysinfo_logo_mask(rp, x + 1, y + 1, shadow_text_color());
    draw_xsysinfo_logo_mask(rp, x, y, COLOR_HIGHLIGHT);
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
    WORD x, y;

    draw_panel(0, 0, 640, HEADER_HEIGHT, NULL);

    /* Title bar background with a low-color stipple, SysInfo-style */
    if (decorative_dots_available()) {
		// Gradient fill instead:
		draw_gradient_3(1, 1, 640-2, HEADER_HEIGHT-2, COLOR_BAR_FILL, COLOR_BUTTON_DARK, COLOR_BAR_YOU);
	/*
        for (y = 3; y < HEADER_HEIGHT - 2; y += 4) {
            x = 3 + ((y & 4) ? 2 : 0);
            draw_dotted_row(x, y, SCREEN_WIDTH - 2, 0x8888);
        }
    */
    } else {
		SetAPen(rp, COLOR_BAR_FILL);
		RectFill(rp, 1, 1, SCREEN_WIDTH - 2, HEADER_HEIGHT - 2);
	}

    draw_xsysinfo_logo(7, 3);

    SetDrMd(rp, JAM1);

    /* Title text */
    snprintf(title, sizeof(title), "%s - %s", XSYSINFO_VERSION, get_string(MSG_TAGLINE));
    title_len = strlen(title);
    title_width = title_len * 8;
    title_x = title_area_x;
    if (title_width < title_area_w) {
        title_x += (title_area_w - title_width) / 2;
    }

    /* Subtitle, centered in the same remaining title-bar space */
    snprintf(subtitle, sizeof(subtitle), "%s https://github.com/reinauer/xsysinfo",
             get_string(MSG_CONTACT_LABEL));
    subtitle_len = strlen(subtitle);
    subtitle_width = subtitle_len * 8;
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
    WORD px, py, dot_start;
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

        if (decorative_dots_available()) {
			draw_gradient_3(x+2, y+2, w-4, h-4, COLOR_BUTTON_DARK, COLOR_TITLE_BG, COLOR_BACKGROUND);
		/*
            dot_start = x + 4 + title_len * 8 + 8;
            for (py = y + 3; py <= y + h - 3; py += 4) {
                px = dot_start + ((py & 4) ? 4 : 0);
                draw_dotted_row(px, py, x + w - 3, 0x8080);
            }
        */
        }

        SetDrMd(rp, JAM1);
        SetAPen(rp, shadow_text_color());
        Move(rp, x + 5, y + h - 3);
        Text(rp, (CONST_STRPTR)title, title_len);
        SetAPen(rp, COLOR_HIGHLIGHT);
        Move(rp, x + 4, y + h - 4);
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
        text_x = btn->x + (btn->width - text_len * 8) / 2;
        text_y = btn->y + (btn->height + 6) / 2;

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
    text_y = btn->y + (btn->height + 6) / 2;

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

/*
 * Draw a scroll bar (prop gadget style)
 */
void draw_scroll_bar(WORD x, WORD y, WORD w, WORD h, ULONG pos, ULONG total, ULONG visible)
{
    struct RastPort *rp = app->rp;
    WORD knob_y, knob_h;
    WORD track_h = h;

    /* Draw recessed track background */
    SetAPen(rp, COLOR_BUTTON_DARK);
    RectFill(rp, x, y, x + w - 1, y + h - 1);

    /* Draw 3D recessed border for track */
    draw_3d_box(x, y, w, h, TRUE);

    /* Calculate knob size and position */
    if (total <= visible) {
        /* Everything fits, knob fills track */
        knob_y = y + 1;
        knob_h = track_h - 2;
    } else {
        /* Calculate proportional knob size (minimum 8 pixels) */
        knob_h = (visible * (track_h - 2)) / total;
        if (knob_h < 8) knob_h = 8;

        /* Calculate knob position */
        WORD travel = track_h - 2 - knob_h;
        knob_y = y + 1 + (pos * travel) / (total - visible);
    }

    /* Draw knob background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x + 1, knob_y, x + w - 2, knob_y + knob_h - 1);

    /* Draw raised 3D border on knob */
    draw_3d_box(x + 1, knob_y, w - 2, knob_h, FALSE);
}

/*
 * Draw text at position
 */
void draw_text(WORD x, WORD y, const char *text, UBYTE color)
{
    struct RastPort *rp = app->rp;
    WORD len = strlen(text);

    SetAPen(rp, color);
    SetBPen(rp, COLOR_PANEL_BG);
    TextLength(rp, (CONST_STRPTR)text, len);
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)text, len);
}

/*
 * Draw text right-aligned
 */
void draw_text_right(WORD x, WORD y, WORD width, const char *text, UBYTE color)
{
    struct RastPort *rp = app->rp;
    WORD text_x = x + width - TextLength(rp, (CONST_STRPTR)text, strlen(text));

    draw_text(text_x, y, text, color);
}

/*
 * Draw text centered within a width
 */
void draw_text_centered(WORD x, WORD y, WORD width, const char *text, UBYTE color)
{
    struct RastPort *rp = app->rp;
    WORD text_x = x + (width - TextLength(rp, (CONST_STRPTR)text,
                                           strlen(text))) / 2;

    draw_text(text_x, y, text, color);
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

static void draw_hardware_overview_row(WORD y, const char *label,
                                       const char *value)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, HARDWARE_PANEL_X + 2, y - 7,
             HARDWARE_PANEL_X + HARDWARE_PANEL_W - 3, y);
    draw_label_value(HARDWARE_PANEL_X + 4, y, label, value,
                     HARDWARE_OVERVIEW_VALUE_OFFSET);
}

static void format_cpu_value(char *buffer, size_t size)
{
    char mhz_buf[16];

    if (hw_info.cpu_mhz > 0)
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz, FALSE);
    else
        mhz_buf[0] = 0;

    if (hw_info.cpu_revision[0] != '\0' &&
        strcmp(hw_info.cpu_revision, "N/A") != 0) {
        snprintf(buffer, size, "%s (%s) %s",
                 hw_info.cpu_string, hw_info.cpu_revision, mhz_buf);
    } else {
        snprintf(buffer, size, "%s %s", hw_info.cpu_string, mhz_buf);
    }
}

static void format_fpu_value(char *buffer, size_t size)
{
    if (hw_info.fpu_type != FPU_NONE && hw_info.fpu_mhz > 0) {
        char mhz_buf[16];

        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.fpu_mhz, FALSE);
        if (hw_info.fpu_enabled) {
            snprintf(buffer, size, "%s %s", hw_info.fpu_string, mhz_buf);
        } else {
            snprintf(buffer, size, "%s %s (%s)",
                     hw_info.fpu_string, mhz_buf, get_string(MSG_OFF));
        }
    } else {
        /* The (Off) suffix only makes sense when an FPU is present;
         * a plain 68000 has nothing to switch off (issue #26). */
        if (hw_info.fpu_enabled || hw_info.fpu_type == FPU_NONE) {
            snprintf(buffer, size, "%s", hw_info.fpu_string);
        } else {
            snprintf(buffer, size, "%s (%s)",
                     hw_info.fpu_string, get_string(MSG_OFF));
        }
    }
}

static void refresh_hardware_benchmark_rows(void)
{
    char buffer[74];
    WORD y;

    if (app->current_view != VIEW_MAIN ||
        app->hardware_type != HARDWARE_STD) {
        return;
    }

    y = HARDWARE_PANEL_Y + 24 + 5 * 8;

    format_cpu_value(buffer, sizeof(buffer));
    draw_hardware_overview_row(y, get_string(MSG_CPU_MHZ), buffer);
    y += 8;

    format_fpu_value(buffer, sizeof(buffer));
    draw_hardware_overview_row(y, get_string(MSG_FPU), buffer);

    y += 16;
    draw_hardware_overview_row(y, get_string(MSG_COMMENT), hw_info.comment);
}

static void format_mmu_value(char *buffer, size_t size)
{
    if (hw_info.mmu_enabled) {
        snprintf(buffer, size, "%s (%s)",
                 hw_info.mmu_string, get_string(MSG_IN_USE));
    } else {
        copy_string(buffer, hw_info.mmu_string, size);
    }
}

static void format_mmu_address(char *buffer, size_t size, ULONG address)
{
    APTR phys = mmu_physical_address((APTR)address);

    if (phys != (APTR)address) {
        snprintf(buffer, size, "$%08lX ->%s", (unsigned long)address,
                 get_location_string(determine_mem_location(phys)));
    } else {
        snprintf(buffer, size, "$%08lX", (unsigned long)address);
    }
}

/* Forward declaration */
static void update_software_list(void);

/*
 * Draw software panel (libraries/devices/resources)
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

    update_software_list();
}

/*
 * Rebuild hardware-page buttons and redraw the hardware panel.
 */
static void update_hardware_text(void)
{
    Button *hw_cycle_btn;

    update_button_states();
    hw_cycle_btn = find_button(BTN_HARDWARE_CYCLE);

    if (hw_cycle_btn) {
        const char *new_hw_label = get_hardware_page_label();
        if (hw_cycle_btn->label != new_hw_label) {
            hw_cycle_btn->label = new_hw_label;
            draw_cycle_button(hw_cycle_btn);
        }
    }
    draw_hardware_panel();
    draw_bottom_buttons();
}

/*
 * Update software list content only (no panel redraw)
 * Used for partial refresh when cycling through types
 */
static void update_software_list(void)
{
    struct RastPort *rp = app->rp;
    SoftwareList *list;
    ULONG i;
    WORD y;
    WORD list_top = SOFTWARE_PANEL_Y + 22;
    char buffer[128];

    /* Get current list */
    switch (app->software_type) {
        case SOFTWARE_LIBRARIES:
            list = &libraries_list;
            break;
        case SOFTWARE_DEVICES:
            list = &devices_list;
            break;
        case SOFTWARE_RESOURCES:
            list = &resources_list;
            break;
        case SOFTWARE_MMU:
            list = &mmu_list;
            break;
        default:
            return;
    }

    /* Clear list area (stop before scroll bar at -14) */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, SOFTWARE_PANEL_X + 2, list_top - 7,
             SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 16,
             SOFTWARE_PANEL_Y + SOFTWARE_PANEL_H - 2);

    /* Update cycle button only if label changed */
    Button *cycle_btn = find_button(BTN_SOFTWARE_CYCLE);
    if (cycle_btn) {
        const char *new_label = app->software_type == SOFTWARE_LIBRARIES ?
                                    get_string(MSG_LIBRARIES) :
                                app->software_type == SOFTWARE_DEVICES ?
                                    get_string(MSG_DEVICES) :
                                app->software_type == SOFTWARE_RESOURCES ?
                                    get_string(MSG_RESOURCES):
                                    get_string(MSG_MMU_ENTRIES);
        if (cycle_btn->label != new_label) {
            cycle_btn->label = new_label;
            draw_cycle_button(cycle_btn);
        }
    }


    /* Draw scroll arrows with triangles */
    Button *up_btn = find_button(BTN_SOFTWARE_UP);
    Button *down_btn = find_button(BTN_SOFTWARE_DOWN);
    Button *scrollbar_btn = find_button(BTN_SOFTWARE_SCROLLBAR);

    if (up_btn) {
        draw_scroll_arrow(up_btn->x, up_btn->y, up_btn->width, up_btn->height,
                          TRUE, up_btn->pressed);
    }
    if (down_btn) {
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
    SetBPen(rp, COLOR_PANEL_BG);
    y = list_top;
    for (i = app->software_scroll;
         i < list->count && i < (ULONG)(app->software_scroll + SOFTWARE_LIST_LINES);
         i++) {

        SoftwareEntry *entry = &list->entries[i];

        if (app->software_type == SOFTWARE_MMU) {
            if (strlen(entry->name) > 49) {
                entry->name[48] = '+';
                entry->name[49] = '\0';
            }
            snprintf(buffer, 50, "%-49s", entry->name);
            SetAPen(rp, COLOR_TEXT);
            Move(rp, SOFTWARE_PANEL_X + 4, y);
            TightText(rp, SOFTWARE_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 8);
        }
        else {
            /* Name */
            SetAPen(rp, COLOR_TEXT);
            draw_text_clipped(SOFTWARE_PANEL_X + 4, y, entry->name,
                              126 - 4);

            /* Location */
            draw_text_clipped(SOFTWARE_PANEL_X + 126, y,
                              get_location_string(entry->location),
                              200 - 126);

            /* Address */
            snprintf(buffer, 12, "$%08lX", (unsigned long)entry->address);
            SetAPen(rp, COLOR_HIGHLIGHT);
            draw_text_clipped(SOFTWARE_PANEL_X + 200, y, buffer,
                              284 - 200);

            /* Version */
            snprintf(buffer, sizeof(buffer), "V%d.%d", entry->version, entry->revision);
            draw_text_clipped(SOFTWARE_PANEL_X + 284, y, buffer,
                              (SOFTWARE_PANEL_W - 16) - 284);
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
    if (max_value == 0 || value == 0) return 0;

    if (app->bar_scale == SCALE_EXPAND) {
        /* Linear scale */
        return (ULONG)(((unsigned long long)value * SPEED_BAR_MAX_WIDTH) / max_value);
    } else {
        /* Shrink mode: A3000 at 100% */
        ULONG a4000_value = reference_systems[REF_A3000].dhrystones;
        ULONG ref_width = SPEED_BAR_MAX_WIDTH;
        if (value <= a4000_value) {
            return (ULONG)(((unsigned long long)value * ref_width) / a4000_value);
        }
        return ref_width +
            (ULONG)(((unsigned long long)(value - a4000_value) * ref_width) /
            (max_value - a4000_value));
    }
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

static WORD draw_micro_glyph(WORD x, WORD y, int glyph)
{
    struct RastPort *rp = app->rp;
    WORD row, col;

    for (row = 0; row < 5; row++) {
        UBYTE bits = micro_glyphs[glyph][row];
        for (col = 0; col < 3; col++) {
            if (bits & (4 >> col)) {
                WritePixel(rp, x + col, y + row);
            }
        }
    }
    return x + 4;
}

static void draw_micro_number(WORD x, WORD y, ULONG value, BOOL kilo)
{
    char buf[12];
    int i;

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    for (i = 0; buf[i]; i++) {
        x = draw_micro_glyph(x, y, buf[i] - '0');
    }
    if (kilo && value > 0) {
        draw_micro_glyph(x, y, 10);
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
    WORD x;

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

    /* Dotted baseline across the bar width */
    SetAPen(rp, COLOR_BUTTON_DARK);
    for (x = x0; x < x0 + SPEED_BAR_MAX_WIDTH; x += 4) {
        WritePixel(rp, x, y + 4);
    }

    for (v = 0; ; v += step) {
        ULONG w = scale_bar_width(v, max_value);
        ULONG label = kilo ? v / 1000 : v;
        char buf[12];
        WORD label_w;

        if (w >= SPEED_BAR_MAX_WIDTH) break;
        x = x0 + (WORD)w;

        /* Tick mark */
        SetAPen(rp, COLOR_TEXT);
        Move(rp, x, y + 2);
        Draw(rp, x, y + 4);

        /* Label to the right of the tick, interrupting the dotted line */
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)label);
        label_w = strlen(buf) * 4 + ((kilo && label) ? 4 : 0);
        if (x + 3 + label_w <= x0 + SPEED_BAR_MAX_WIDTH) {
            SetAPen(rp, COLOR_PANEL_BG);
            RectFill(rp, x + 2, y, x + 2 + label_w, y + 4);
            SetAPen(rp, COLOR_TEXT);
            draw_micro_number(x + 3, y, label, kilo);
        }
    }

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

    if (app->bar_scale == SCALE_EXPAND) {
        max_value = get_max_dhrystones();
    } else {
        ULONG a4000_value = reference_systems[REF_A4000].dhrystones;
        max_value = a4000_value ? a4000_value * 2 : 1;
    }

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

    /* Memory speeds header */
    y += 8;

    SetAPen(rp, COLOR_TEXT);
    snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_MEM_SPEED_HEADER));
    TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 4);

    /* Memory speed values */
    y += 8;
    {
        char chip_str[8], fast_str[8], rom_str[8];

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

        snprintf(buffer, sizeof(buffer), "%-6s %-6s %-6s  %s",
                 chip_str, fast_str, rom_str, get_string(MSG_MEM_SPEED_UNIT));
    }
    SetAPen(rp, COLOR_HIGHLIGHT);
    TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 4);
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

/*
 * Draw hardware panel
 */

static void draw_hardware_panel(void)
{
    WORD y;
    char buffer[74];

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

    y = HARDWARE_PANEL_Y + 24;
    if (app->hardware_type == HARDWARE_STD) {
        /* Clock */
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_CLOCK), hw_info.clock_string,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* DMA/Gfx */
        switch (hw_info.agnus_type) {
            case AGNUS_OCS_NTSC:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_OCS_NTSC));
                break;
            case AGNUS_OCS_PAL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_OCS_PAL));
                break;
            case AGNUS_OCS_FAT_NTSC:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_OCS_FAT_NTSC));
                break;
            case AGNUS_OCS_FAT_PAL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_OCS_FAT_PAL));
                break;
            case AGNUS_ECS_2MB_NTSC:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_2MB_NTSC));
                break;
            case AGNUS_ECS_2MB_PAL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_2MB_PAL));
                break;
            case AGNUS_ECS_NTSC:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_NTSC));
                break;
            case AGNUS_ECS_B_NTSC:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_B_NTSC));
                break;
            case AGNUS_ECS_PAL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_PAL));
                break;
            case AGNUS_ECS_B_PAL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_ECS_B_PAL));
                break;
            case AGNUS_ALICE_NTSC:
                snprintf(buffer, sizeof(buffer), "%s Rev. %X",
                      get_string(MSG_AGNUS_ALICE_NTSC), (hw_info.agnus_rev&0xF));
                break;
            case AGNUS_ALICE_PAL:
                snprintf(buffer, sizeof(buffer), "%s Rev. %X",
                      get_string(MSG_AGNUS_ALICE_PAL), (hw_info.agnus_rev&0xF));
                break;
            case AGNUS_SAGA:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_AGNUS_SAGA));
                break;
            case AGNUS_UNKNOWN:
            default:
                snprintf(buffer, sizeof(buffer), "%s %2X",
                      get_string(MSG_AGNUS_UNKNOWN), hw_info.agnus_rev);
                break;
        }
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_DMA_GFX), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Mode */
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_MODE), hw_info.mode_string,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Display */

        switch (hw_info.denise_type) {
            case DENISE_OCS:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_DENISE_OCS));
                break;
            case DENISE_ECS:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_DENISE_ECS));
                break;
            case DENISE_LISA:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_DENISE_LISA));
                break;
            case DENISE_ISABEL:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_DENISE_SAGA));
                break;
            case DENISE_UNKNOWN:
            default:
                snprintf(buffer, sizeof(buffer), "%s %02X",
                      get_string(MSG_DENISE_UNKNOWN), hw_info.denise_rev);
                break;
        }

        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_DISPLAY), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Sound */
        switch (hw_info.paula_type) {
            case PAULA_ORIG:
                snprintf(buffer, sizeof(buffer), "%s",
                      get_string(MSG_PAULA_ORIG));
                break;
            case PAULA_SAGA:
                snprintf(buffer, sizeof(buffer), "%s (ID: %02X)",
                      get_string(MSG_PAULA_SAGA),hw_info.paula_rev);
                break;
            case PAULA_UNKNOWN:
                snprintf(buffer, sizeof(buffer), "%s (ID: %02X)",
                      get_string(MSG_PAULA_UNKNOWN),hw_info.paula_rev);
                break;
        }
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_SOUND_SYSTEM), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* CPU/MHz */
        format_cpu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_CPU_MHZ), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* FPU */
        format_fpu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_FPU), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* MMU */
        format_mmu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_MMU), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Comment */
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_COMMENT), hw_info.comment,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Frequencies - left column continues */
        {
            unsigned long long horiz_khz =
                ((unsigned long long)hw_info.horiz_freq * 100ULL) / 1000ULL;
            format_scaled(buffer, sizeof(buffer), (ULONG)horiz_khz, FALSE);
        }
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_HORIZ_KHZ), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);

        y += 8;

        /* EClock */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.eclock_freq);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_ECLOCK_HZ), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Vert Hz */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.vert_freq);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_VERT_HZ), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Supply Hz */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.supply_freq);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_SUPPLY_HZ), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Ramsey */
        format_ramsey_rev_string(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_RAMSEY_REV), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Gary */
        switch (hw_info.gary_type) {
            case GARY_A1000:
                copy_string(buffer, get_string(MSG_GARY_A1000), sizeof(buffer));
                break;
            case GARY_A500:
                copy_string(buffer, get_string(MSG_GARY_A500), sizeof(buffer));
                break;
            case GAYLE:
                snprintf(buffer, sizeof(buffer), "%s %02X", get_string(MSG_GAYLE), hw_info.gary_rev);
                break;
            case FAT_GARY:
                copy_string(buffer, get_string(MSG_FAT_GARY), sizeof(buffer));
                break;
            case GARY_UNKNOWN:
            default:
                copy_string(buffer, get_string(MSG_GARY_UNKNOWN), sizeof(buffer));
                break;
        }
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_GARY_REV), buffer,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        /* Card Slot */
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_CARD_SLOT), hw_info.card_slot_string,
                         HARDWARE_OVERVIEW_VALUE_OFFSET);
        y += 8;

        if (strcmp(hw_info.amiga_model_string, get_string(MSG_NA)) != 0) {
            draw_label_value(HARDWARE_PANEL_X + 4, y,
                             "Amiga", hw_info.amiga_model_string,
                             HARDWARE_OVERVIEW_VALUE_OFFSET);
        }
    } else if (app->hardware_type == HARDWARE_CPU) {
        WORD cache_y;

        format_cpu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_CPU_MHZ), buffer, 80);
        y += 8;

        format_fpu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_FPU), buffer, 80);
        y += 8;

        format_mmu_value(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_MMU), buffer, 80);
        y += 8;

        format_mmu_address(buffer, sizeof(buffer), hw_info.vbr);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_VBR), buffer, 80);
        y += 8;

        format_mmu_address(buffer, sizeof(buffer), hw_info.ssp);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_SSP), buffer, 80);
        y += 8;

        format_mmu_address(buffer, sizeof(buffer), (ULONG)SysBase);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         "ExecBase", buffer, 80);
        y += 8;

        format_mmu_address(buffer, sizeof(buffer), 0);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         "Page 0", buffer, 80);
        y += 16;

        cache_y = CACHE_LABEL_Y0;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_ICACHE), NULL, 0,
                             CACHE_BTN_X - 4);
        cache_y += CACHE_ROW_STEP;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_DCACHE), NULL, 0,
                             CACHE_BTN_X - 4);
        cache_y += CACHE_ROW_STEP;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_IBURST), NULL, 0,
                             CACHE_BTN_X - 4);
        cache_y += CACHE_ROW_STEP;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_DBURST), NULL, 0,
                             CACHE_BTN_X - 4);
        cache_y += CACHE_ROW_STEP;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_CBACK), NULL, 0,
                             CACHE_BTN_X - 4);
        cache_y += CACHE_ROW_STEP;
        draw_label_value_max(HARDWARE_PANEL_X + 4, cache_y,
                             get_string(MSG_SUPER_SCALAR), NULL, 0,
                             CACHE_BTN_X - 4);
        draw_cache_buttons();
    } else { // extended hw-info
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_EXT_INFO), NULL, 120);
        y += 8;
        /* Ramsey */
        format_ramsey_rev_string(buffer, sizeof(buffer));
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_RAMSEY_REV), buffer,
                         HARDWARE_CHIPSET_VALUE_OFFSET);
        y += 8;
        if (hw_info.ramsey_rev) {
            /* Ramsey status */
            draw_label_value(HARDWARE_PANEL_X + 4, y,
                             get_string(MSG_RAMSEY_CTRL), NULL, 120);
            y += 8;

            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_page_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_PAGE), buffer, 110);
            y += 8;

            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_burst_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_BURST), buffer, 110);
            y += 8;
            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_wrap_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_WRAP), buffer, 110);
            y += 8;
            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_size_1M ? get_string(MSG_1M) : get_string(MSG_256K));
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_SIZE), buffer, 110);
            y += 8;
            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_skip_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_SKIP), buffer, 110);
            y += 8;
            switch (hw_info.ramsey_refresh_rate) {
                case 0:
                    copy_string(buffer, "154 clk", sizeof(buffer));
                    break;
                case 1:
                    copy_string(buffer, "238 clk", sizeof(buffer));
                    break;
                case 2:
                    copy_string(buffer, "380 clk", sizeof(buffer));
                    break;
                default:
                    copy_string(buffer, "off", sizeof(buffer));
                    break;
               }
            draw_label_value(HARDWARE_PANEL_X + 18, y,
                             get_string(MSG_RAMSEY_REFRESH), buffer, 110);
            y += 8;
               draw_label_value(HARDWARE_PANEL_X + 4, y,
                             get_string(MSG_NV_RAM), NULL, 120);
            y += 8;

            if (hw_info.battMemData.valid_data) {
                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.amnesia_amiga ? get_string(MSG_YES) : get_string(MSG_NO));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_AMNESIA), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.amnesia_shared ? get_string(MSG_YES) : get_string(MSG_NO));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_SHARED_AMNESIA), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.long_timeout ? get_string(MSG_LONG) : get_string(MSG_SHORT));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_TIMEOUT), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.scan_luns ? get_string(MSG_ON) : get_string(MSG_OFF));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_SCAN_LUN), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.sync_transfer ? get_string(MSG_ON) : get_string(MSG_OFF));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_SYNC_TRANS), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.fast_sync_transfer ? get_string(MSG_ON) : get_string(MSG_OFF));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_FAST_SYNC), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.tagged_queuing ? get_string(MSG_ON) : get_string(MSG_OFF));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_QUEUING), buffer, 110);
                y += 8;

                snprintf(buffer, sizeof(buffer), "%d", hw_info.battMemData.scsi_id);
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                                get_string(MSG_SCSI_HOST_ID), buffer, 110);
                y += 8;
            }
            else {
                copy_string(buffer, get_string(MSG_NA), sizeof(buffer));
                draw_label_value(HARDWARE_PANEL_X + 18, y,
                            buffer, NULL, 120);
                y += 8;
            }
        }
        if (hw_info.sdmac_rev  && hw_info.gary_type == FAT_GARY) { //
            snprintf(buffer, sizeof(buffer), "%s REV %02X", (hw_info.is_A4000T? get_string(MSG_NCR_53C710) : get_string(MSG_SDMAC)), hw_info.sdmac_rev);
        } else {
            copy_string(buffer, get_string(MSG_NA), sizeof(buffer));
        }
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_SDMAC_REV), buffer,
                         HARDWARE_CHIPSET_VALUE_OFFSET);
        y += 8;

    }
}

/*
 * Draw bottom buttons
 */
static void draw_bottom_buttons(void)
{
    int i;
    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_QUIT && buttons[i].id <= BTN_PRINT) {
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
    /* Refresh all cache states from hardware */
    refresh_cache_status();

    /* Update all labels based on current state */
    snprintf(icache_label, sizeof(icache_label), "%s",
             hw_info.has_icache ?
                 (hw_info.icache_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));
    snprintf(dcache_label, sizeof(dcache_label), "%s",
             hw_info.has_dcache ?
                 (hw_info.dcache_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));
    snprintf(iburst_label, sizeof(iburst_label), "%s",
             hw_info.has_iburst ?
                 (hw_info.iburst_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));
    snprintf(dburst_label, sizeof(dburst_label), "%s",
             hw_info.has_dburst ?
                 (hw_info.dburst_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));
    snprintf(cback_label, sizeof(cback_label), "%s",
             hw_info.has_copyback ?
                 (hw_info.copyback_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));
    snprintf(super_scalar_label, sizeof(super_scalar_label), "%s",
             hw_info.has_super_scalar ?
                 (hw_info.super_scalar_enabled ? get_string(MSG_ON) : get_string(MSG_OFF)) :
                 get_string(MSG_NA));

    /* Update all button pressed states */
    set_button_pressed(BTN_ICACHE, hw_info.icache_enabled);
    set_button_pressed(BTN_DCACHE, hw_info.dcache_enabled);
    set_button_pressed(BTN_IBURST, hw_info.iburst_enabled);
    set_button_pressed(BTN_DBURST, hw_info.dburst_enabled);
    set_button_pressed(BTN_CBACK, hw_info.copyback_enabled);
    set_button_pressed(BTN_SUPER_SCALAR, hw_info.super_scalar_enabled);
    update_cache_button_enabled_states();

    /* Redraw all cache buttons */
    draw_cache_buttons();
}

static void show_timed_overlay(const char *message, ULONG ticks)
{
    struct RastPort *rp = app->rp;
    WORD text_len = strlen(message);
    WORD dialog_w = (text_len * 8) + 32;
    WORD dialog_h = 28;
    WORD dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
    WORD dialog_y = (app->screen_height - dialog_h) / 2;

    SetAPen(rp, COLOR_BAR_YOU);
    RectFill(rp, dialog_x, dialog_y,
             dialog_x + dialog_w - 1, dialog_y + dialog_h - 1);

    draw_3d_box(dialog_x, dialog_y, dialog_w, dialog_h, FALSE);

    SetAPen(rp, COLOR_BUTTON_LIGHT);
    SetBPen(rp, COLOR_BAR_YOU);
    Move(rp, dialog_x + (dialog_w - text_len * 8) / 2, dialog_y + 16);
    Text(rp, (CONST_STRPTR)message, text_len);

    Delay(ticks);
    redraw_current_view();
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

        case VIEW_SCSI:
            scsi_view_handle_button(btn_id);
            break;
    }
}

/*
 * Handle click on scrollbar - scroll based on click position
 */
void handle_scrollbar_click(WORD mx __attribute__((unused)), WORD my)
{
    Button *scrollbar_btn = find_button(BTN_SOFTWARE_SCROLLBAR);
    SoftwareList *list;
    WORD knob_h;
    WORD track_h;
    LONG max_scroll;

    if (!scrollbar_btn) return;

    /* Get current list */
    switch (app->software_type) {
        case SOFTWARE_LIBRARIES:
            list = &libraries_list;
            break;
        case SOFTWARE_DEVICES:
            list = &devices_list;
            break;
        case SOFTWARE_RESOURCES:
            list = &resources_list;
            break;
        case SOFTWARE_MMU:
            list = &mmu_list;
            break;
        default:
            return;
    }

    max_scroll = (LONG)list->count - SOFTWARE_LIST_LINES;
    if (max_scroll <= 0) return;

    track_h = scrollbar_btn->height;

    /* Calculate knob size */
    knob_h = (SOFTWARE_LIST_LINES * (track_h - 2)) / list->count;
    if (knob_h < 8) knob_h = 8;

    /* When dragging, directly calculate scroll position from mouse Y */
    /* Center the knob on the mouse position */
    WORD rel_y = my - scrollbar_btn->y - knob_h / 2;
    WORD travel = track_h - 2 - knob_h;

    if (travel > 0) {
        LONG new_scroll = (rel_y * max_scroll) / travel;
        if (new_scroll < 0) new_scroll = 0;
        if (new_scroll > max_scroll) new_scroll = max_scroll;
        if (new_scroll != app->software_scroll) {
            app->software_scroll = new_scroll;
            update_software_list();
        }
    }
}

/*
 * Switch to a different view
 */
void switch_to_view(ViewMode view)
{
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
            app->board_scroll = 0;
            break;
        default:
            break;
    }

    redraw_current_view();
}

/* Blank pointer sprite data for hiding mouse cursor */
static UWORD blank_pointer[] = {
    0x0000, 0x0000,  /* Reserved */
    0x0000, 0x0000,  /* 1 line of empty data */
    0x0000, 0x0000   /* Reserved */
};

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
        ULONG flags = BMF_CLEAR;

        if (app->use_custom_screen)
            flags |= BMF_STANDARD;

        overlay_backup.bitmap = AllocBitMap(w, h, depth, flags,
                                            app->rp->BitMap);
        if (overlay_backup.bitmap) {
            overlay_backup.allocated_bitmap = TRUE;
        }
    }

    if (!overlay_backup.bitmap) {
        if (depth > 8)
            return FALSE;

        InitBitMap(&overlay_backup.legacy_bitmap, depth, w, h);
        overlay_backup.bitmap = &overlay_backup.legacy_bitmap;
    }

    if (!overlay_backup.allocated_bitmap) {
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
    WORD dialog_w = (text_len * 8) + 32;
    WORD dialog_h = 28;
    WORD dialog_x = area_x + (area_w - dialog_w) / 2;
    WORD dialog_y = area_y + (area_h - dialog_h) / 2;

    save_overlay_area(dialog_x, dialog_y, dialog_w, dialog_h);

    /* Hide mouse pointer with blank sprite */
    SetPointer(app->window, blank_pointer, 1, 1, 0, 0);

    /* Draw shadow */
    //SetAPen(rp, COLOR_BUTTON_DARK);
    //RectFill(rp, dialog_x + 2, dialog_y + 2, dialog_x + dialog_w + 1, dialog_y + dialog_h + 1);

    /* Draw red background */
    SetAPen(rp, COLOR_BAR_YOU);  /* Red color */
    RectFill(rp, dialog_x, dialog_y, dialog_x + dialog_w - 1, dialog_y + dialog_h - 1);

    /* Draw 3D border */
    draw_3d_box(dialog_x, dialog_y, dialog_w, dialog_h, FALSE);

    /* Draw centered message */
    SetAPen(rp, COLOR_BUTTON_LIGHT);  /* White text */
    SetBPen(rp, COLOR_BAR_YOU);
    TextLength(rp, (CONST_STRPTR)message, text_len);
    Move(rp, dialog_x + (dialog_w - text_len * 8) / 2, dialog_y + 16);
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
    BOOL restored;

    /* Restore mouse pointer */
    ClearPointer(app->window);

    restored = restore_overlay_area();
    if (!restored) {
        /* Fallback for deep/unknown bitmaps or allocation failure. */
        redraw_current_view();
    }
}

/*
 * Draw just the text field contents (for fast updates while typing)
 */
static void draw_requester_field(WORD field_x, WORD field_y, WORD field_w, WORD field_h,
                                 const char *filename, ULONG cursor_pos)
{
    struct RastPort *rp = app->rp;
    WORD cursor_x;
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
    if (cursor_x > field_x + field_w - 10) {
        cursor_x = field_x + field_w - 10;
    }
    SetAPen(rp, COLOR_TEXT);
    RectFill(rp, cursor_x, field_y + 2, cursor_x + 7, field_y + field_h - 3);
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

    save_overlay_area(dialog_x, dialog_y, dialog_w, dialog_h);

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
