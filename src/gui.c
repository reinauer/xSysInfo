/*
 * xSysInfo - GUI rendering and event handling
 */

#include <string.h>
#include <stdio.h>

#include <graphics/rastport.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

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
extern MemoryRegionList memory_regions;
extern DriveList drive_list;
extern BoardList board_list;

/* Button definitions for main view */
#define MAX_BUTTONS 32
Button buttons[MAX_BUTTONS];
int num_buttons = 0;

/* Forward declarations */
static void draw_header(void);
static void draw_software_panel(void);
static void draw_speed_panel(void);
static void draw_hardware_panel(void);
static void draw_bottom_buttons(void);
static void draw_cache_buttons(void);
static void draw_cache_status(void);
static void add_button(WORD x, WORD y, WORD w, WORD h,
                       const char *label, ButtonID id, BOOL enabled);
static void clear_buttons(void);

void format_scaled(char *buffer, size_t size, ULONG value_x100)
{
    snprintf(buffer, size, "%lu.%02lu",
             (unsigned long)(value_x100 / 100),
             (unsigned long)(value_x100 % 100));
}

void TightText(struct RastPort *rp, int x, int y, CONST_STRPTR str, int charGap, int spaceWidth)
{
    int currentX = x;

    Move(rp, x, y);

    for (int i = 0; str[i]; i++) {
        if (str[i] == ' ') {
            currentX += spaceWidth;
        } else {
            Move(rp, currentX, y);
            Text(rp, &str[i], 1);

            // Get character width and add small gap
            int charWidth = TextLength(rp, &str[i], 1);
            currentX += charWidth + charGap;
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
static void add_button(WORD x, WORD y, WORD w, WORD h,
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
    } else {
        draw_button(btn);
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
            /* Bottom row buttons */
            add_button(177, 176, 60, 11,
                       get_string(MSG_BTN_QUIT), BTN_QUIT, TRUE);
            add_button(239, 176, 60, 11,
                       get_string(MSG_BTN_MEMORY), BTN_MEMORY, TRUE);
            add_button(301, 176, 60, 11,
                       get_string(MSG_BTN_DRIVES), BTN_DRIVES, TRUE);
            add_button(177, 187, 60, 11,
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
                           get_string(MSG_RESOURCES),
                       BTN_SOFTWARE_CYCLE, TRUE);

            /* Software scroll buttons (arrows on right side) */
            add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
                       SOFTWARE_PANEL_Y + 17, 12, 10,
                       NULL, BTN_SOFTWARE_UP, TRUE);   /* Up arrow */
            add_button(SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 14,
                       SOFTWARE_PANEL_Y + 17 + 10, 12, SOFTWARE_PANEL_H - 17 - 10 - 12,
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

            /* Cache buttons */
            add_button(464, 176, 56, 11,
                       get_string(MSG_ICACHE), BTN_ICACHE, hw_info.has_icache);
            add_button(464, 187, 56, 11,
                       get_string(MSG_DCACHE), BTN_DCACHE, hw_info.has_dcache);
            add_button(522, 176, 56, 11,
                       get_string(MSG_IBURST), BTN_IBURST, hw_info.has_iburst);
            add_button(522, 187, 56, 11,
                       get_string(MSG_DBURST), BTN_DBURST, hw_info.has_dburst);
            add_button(580, 176, 56, 11,
                       "CBACK", BTN_CBACK, hw_info.has_copyback);
            add_button(580, 187, 56, 11,
                       "ALL", BTN_ALL, hw_info.has_icache);
            break;

        case VIEW_MEMORY:
            add_button(100, 188, 60, 12,
                       get_string(MSG_BTN_PREV), BTN_MEM_PREV,
                       app->memory_region_index > 0);
            add_button(160, 188, 60, 12,
                       get_string(MSG_BTN_NEXT), BTN_MEM_NEXT,
                       app->memory_region_index < (LONG)memory_regions.count - 1);
            add_button(220, 188, 60, 12,
                       get_string(MSG_BTN_EXIT), BTN_MEM_EXIT, TRUE);
            break;

        case VIEW_DRIVES:
            {
                BOOL scsi_enabled = FALSE;

                ULONG i;
                WORD y = 28;
                for (i = 0; i < drive_list.count && i < 10; i++) {
                    add_button(10, y, 70, 12,
                               drive_list.drives[i].device_name,
                               (ButtonID)(BTN_DRV_DRIVE_BASE + i), TRUE);
                    y += 14;
                }

                if (app->selected_drive >= 0 &&
                    app->selected_drive < (LONG)drive_list.count) {
                    scsi_enabled = drive_list.drives[app->selected_drive].scsi_supported;
                }
                add_button(100, 188, 52, 12,
                           get_string(MSG_BTN_EXIT), BTN_DRV_EXIT, TRUE);
                add_button(160, 188, 52, 12,
                           get_string(MSG_BTN_SCSI), BTN_DRV_SCSI, scsi_enabled);
                add_button(220, 188, 52, 12,
                           get_string(MSG_BTN_SPEED), BTN_DRV_SPEED,
                           app->selected_drive >= 0);
            }
            break;

        case VIEW_BOARDS:
            add_button(20, 188, 60, 12,
                       get_string(MSG_BTN_EXIT), BTN_BOARD_EXIT, TRUE);
            break;

        case VIEW_SCSI:
            add_button(20, 188, 60, 12,
                       get_string(MSG_BTN_EXIT), BTN_SCSI_EXIT, TRUE);
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
    draw_cache_buttons();
}

/*
 * Draw header area
 */
static void draw_header(void)
{
    struct RastPort *rp = app->rp;
    char buffer[128];

    draw_panel(0, 0, 640, HEADER_HEIGHT, NULL);

    /* Title bar background */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, 1, 1, SCREEN_WIDTH - 2, HEADER_HEIGHT - 2);

    /* Title text */
    SetAPen(rp, COLOR_HIGHLIGHT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 8, 9);
    snprintf(buffer, sizeof(buffer), "xSYSINFO %s An Amiga System Information Program",
             XSYSINFO_VERSION);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

    /* Subtitle */
    SetAPen(rp, COLOR_TEXT);
    Move(rp, 8, 19);
    snprintf(buffer, sizeof(buffer), "Contact: %s", get_string(MSG_CONTACT));
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
}

/*
 * Draw 3D panel box
 */
void draw_panel(WORD x, WORD y, WORD w, WORD h, const char *title)
{
    struct RastPort *rp = app->rp;

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
        SetAPen(rp, COLOR_TEXT);
        SetBPen(rp, COLOR_PANEL_BG);
        Move(rp, x + 4, y + 10);
        Text(rp, (CONST_STRPTR)title, strlen(title));
    }
}

/*
 * Draw a 3D recessed or raised box
 */
void draw_3d_box(WORD x, WORD y, WORD w, WORD h, BOOL recessed)
{
    struct RastPort *rp = app->rp;
    UBYTE top_color = recessed ? COLOR_BUTTON_DARK : COLOR_BUTTON_LIGHT;
    UBYTE bot_color = recessed ? COLOR_BUTTON_LIGHT : COLOR_BUTTON_DARK;

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
        Move(rp, text_x, text_y);
        Text(rp, (CONST_STRPTR)btn->label, text_len);
    }
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
    cy = y + h / 2;
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

    SetAPen(rp, color);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)text, strlen(text));
}

/*
 * Draw text right-aligned
 */
void draw_text_right(WORD x, WORD y, WORD width, const char *text, UBYTE color)
{
    WORD text_x = x + width - (strlen(text) * 8);
    draw_text(text_x, y, text, color);
}

/*
 * Draw label: value pair
 * If value is NULL, only the label is drawn
 */
void draw_label_value(WORD x, WORD y, const char *label, const char *value, WORD offset)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)label, strlen(label));

    if (value) {
        SetAPen(rp, COLOR_HIGHLIGHT);
        Move(rp, x + offset, y);
        Text(rp, (CONST_STRPTR)value, strlen(value));
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

    update_software_list();
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
    WORD list_top = SOFTWARE_PANEL_Y + 24;
    WORD list_height = SOFTWARE_LIST_LINES * 8;
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
        default:
            return;
    }

    /* Clear list area */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, SOFTWARE_PANEL_X + 2, list_top - 7,
             SOFTWARE_PANEL_X + SOFTWARE_PANEL_W - 3,
             list_top + list_height - 4);

    /* Update cycle button */
    Button *cycle_btn = find_button(BTN_SOFTWARE_CYCLE);
    if (cycle_btn) {
        cycle_btn->label = app->software_type == SOFTWARE_LIBRARIES ?
                               get_string(MSG_LIBRARIES) :
                           app->software_type == SOFTWARE_DEVICES ?
                               get_string(MSG_DEVICES) :
                               get_string(MSG_RESOURCES);
        draw_button(cycle_btn);
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

        /* Name (truncated if needed) */
        snprintf(buffer, 16, "%-15s", entry->name);
        SetAPen(rp, COLOR_TEXT);
        Move(rp, SOFTWARE_PANEL_X + 4, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* Location */
        snprintf(buffer, 12, "%-10s", get_location_string(entry->location));
        Move(rp, SOFTWARE_PANEL_X + 130, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* Address */
        snprintf(buffer, 12, "$%08lX", (unsigned long)entry->address);
        SetAPen(rp, COLOR_HIGHLIGHT);
        Move(rp, SOFTWARE_PANEL_X + 204, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        /* Version */
        snprintf(buffer, sizeof(buffer), "V%d.%d", entry->version, entry->revision);
        Move(rp, SOFTWARE_PANEL_X + 290, y);
        Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

        y += 8;
    }
}

/*
 * Draw single speed bar
 */
void draw_single_bar(WORD x, WORD y, ULONG value, ULONG max_value, UBYTE color)
{
    struct RastPort *rp = app->rp;
    WORD bar_width;

    /* Draw border */
    draw_3d_box(x - 1, y - 1, SPEED_BAR_MAX_WIDTH + 2, SPEED_BAR_HEIGHT + 2, TRUE);

    if (max_value == 0 || value == 0) return;

    if (app->bar_scale == SCALE_EXPAND) {
        /* Linear scale */
        bar_width = (value * SPEED_BAR_MAX_WIDTH) / max_value;
    } else {
        /* Shrink mode: A4000 at 50% */
        ULONG a4000_value = reference_systems[REF_A4000].dhrystones;
        if (value <= a4000_value) {
            bar_width = (value * (SPEED_BAR_MAX_WIDTH / 2)) / a4000_value;
        } else {
            bar_width = (SPEED_BAR_MAX_WIDTH / 2) +
                        ((value - a4000_value) * (SPEED_BAR_MAX_WIDTH / 2)) /
                        (max_value - a4000_value);
        }
    }

    /* Clamp to max width */
    if (bar_width > SPEED_BAR_MAX_WIDTH) {
        bar_width = SPEED_BAR_MAX_WIDTH;
    }

    /* Draw bar */
    if (bar_width > 0) {
        SetAPen(rp, color);
        RectFill(rp, x, y, x + bar_width - 1, y + SPEED_BAR_HEIGHT - 1);
    }
}

/*
 * Draw speed comparison panel
 */
static void draw_speed_panel(void)
{
    struct RastPort *rp = app->rp;
    WORD y;
    char buffer[64];
    ULONG max_value, cur_value;
    int i;

    draw_panel(SPEED_PANEL_X, SPEED_PANEL_Y,
               SPEED_PANEL_W, SPEED_PANEL_H, NULL);

    draw_panel(SPEED_PANEL_X + 1, SPEED_PANEL_Y + 1,
               SPEED_PANEL_W - 2, 14, get_string(MSG_SPEED_COMPARISONS));

    /* Draw scale toggle button */
    Button *scale_btn = find_button(BTN_SCALE_TOGGLE);
    if (scale_btn) draw_button(scale_btn);

    /* Calculate max value for scaling */
    max_value = get_max_dhrystones();

    /* Draw "You" entry first */
    y = SPEED_PANEL_Y + 22;
    if (bench_results.benchmarks_valid) {
        snprintf(buffer, sizeof(buffer), "%s %lu", get_string(MSG_DHRYSTONES),
             (unsigned long)bench_results.dhrystones);
    } else {
        snprintf(buffer, sizeof(buffer), "%s N/A", get_string(MSG_DHRYSTONES));
    }
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, SPEED_PANEL_X + 4, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));

    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, SPEED_PANEL_X + 150, y);
    Text(rp, (CONST_STRPTR)get_string(MSG_REF_YOU), strlen(get_string(MSG_REF_YOU)));

    if (bench_results.benchmarks_valid) {
	cur_value = bench_results.dhrystones;
    } else {
	cur_value = 0;
    }
    draw_single_bar(SPEED_PANEL_X + 178, y - 5,
                    cur_value, max_value, COLOR_BAR_YOU);

    /* Draw reference systems */
    y += 8;
    for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
        const char *ref_names[] = {
            get_string(MSG_REF_A600),
            get_string(MSG_REF_B2000),
            get_string(MSG_REF_A1200),
            get_string(MSG_REF_A2500),
            get_string(MSG_REF_A3000),
            get_string(MSG_REF_A4000)
        };

        SetAPen(rp, COLOR_TEXT);
        Move(rp, SPEED_PANEL_X + 4, y);
        Text(rp, (CONST_STRPTR)ref_names[i], strlen(ref_names[i]));

	if (bench_results.benchmarks_valid) {
            cur_value = reference_systems[i].dhrystones;
	} else {
	    cur_value = 0;
        }
        draw_single_bar(SPEED_PANEL_X + 178, y - 5,
                        cur_value, max_value, COLOR_BAR_FILL);
        y += 8;
    }

    /* MIPS and MFLOPS */
    //y = SPEED_PANEL_Y + SPEED_PANEL_H - 18;
    if (bench_results.benchmarks_valid) {
        char scaled[16];
        format_scaled(scaled, sizeof(scaled), bench_results.mips);
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_MIPS), scaled);
    } else {
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_MIPS), get_string(MSG_NA));
    }
    SetAPen(rp, COLOR_TEXT);
    TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 4);

    if (hw_info.fpu_type != FPU_NONE && bench_results.benchmarks_valid) {
        char scaled[16];
        format_scaled(scaled, sizeof(scaled), bench_results.mflops);
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_MFLOPS), scaled);
    } else {
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_MFLOPS), get_string(MSG_NA));
    }
    TightText(rp, SPEED_PANEL_X + 84, y, (CONST_STRPTR)buffer, -1, 4);

    /* Chip speed */
    y += 8;
    if (bench_results.benchmarks_valid) {
        char scaled[16];
        format_scaled(scaled, sizeof(scaled), bench_results.chip_speed);
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_CHIP_SPEED), scaled);
    } else {
        snprintf(buffer, sizeof(buffer), "%s %s",
                 get_string(MSG_CHIP_SPEED), get_string(MSG_NA));
    }
    TightText(rp, SPEED_PANEL_X + 4, y, (CONST_STRPTR)buffer, -1, 4);
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

    y = HARDWARE_PANEL_Y + 24;

    /* Clock */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_CLOCK), hw_info.clock_string, 80);
    y += 8;

    /* DMA/Gfx */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_DMA_GFX), hw_info.agnus_string, 80);
    y += 8;

    /* Mode */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_MODE), hw_info.mode_string, 80);
    y += 8;

    /* Display */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_DISPLAY), hw_info.denise_string, 80);
    y += 8;

    /* CPU/MHz */
    if (hw_info.cpu_revision[0] != '\0' &&
        strcmp(hw_info.cpu_revision, "N/A") != 0) {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz);
        snprintf(buffer, sizeof(buffer), "%s (%s) %s",
                 hw_info.cpu_string, hw_info.cpu_revision,
                 mhz_buf);
    } else {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz);
        snprintf(buffer, sizeof(buffer), "%s %s",
                 hw_info.cpu_string, mhz_buf);
    }
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_CPU_MHZ), buffer, 80);
    y += 8;

    /* FPU */
    if (hw_info.fpu_type != FPU_NONE && hw_info.fpu_mhz > 0) {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.fpu_mhz);
        snprintf(buffer, sizeof(buffer), "%s %s",
                 hw_info.fpu_string, mhz_buf);
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_FPU), buffer, 80);
    } else {
        draw_label_value(HARDWARE_PANEL_X + 4, y,
                         get_string(MSG_FPU), hw_info.fpu_string, 80);
    }
    y += 8;

    /* MMU */
    if (hw_info.mmu_enabled) {
        snprintf(buffer, sizeof(buffer), "%s (%s)",
                 hw_info.mmu_string, get_string(MSG_IN_USE));
    } else {
        strncpy(buffer, hw_info.mmu_string, sizeof(buffer) - 1);
    }
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_MMU), buffer, 80);
    y += 8;

    /* VBR */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)hw_info.vbr);
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_VBR), buffer, 80);
    y += 8;

    /* Comment */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_COMMENT), hw_info.comment, 80);
    y += 8;

    /* Frequencies - left column continues */
    {
        unsigned long long horiz_khz =
            ((unsigned long long)hw_info.horiz_freq * 100ULL) / 1000ULL;
        format_scaled(buffer, sizeof(buffer), (ULONG)horiz_khz);
    }
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_HORIZ_KHZ), buffer, 96);

    y += 8;

    /* EClock */
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.eclock_freq);
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_ECLOCK_HZ), buffer, 96);

    /* Right column - cache status labels (values drawn by draw_cache_status) */
    draw_label_value(HARDWARE_PANEL_X + 170, y,
                     get_string(MSG_ICACHE), NULL, 64);
    y += 8;

    /* Ramsey */
    if (hw_info.ramsey_rev) {
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.ramsey_rev);
    } else {
        strncpy(buffer, get_string(MSG_NA), sizeof(buffer) - 1);
    }
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_RAMSEY_REV), buffer, 96);

    draw_label_value(HARDWARE_PANEL_X + 170, y,
                     get_string(MSG_DCACHE), NULL, 64);
    y += 8;

    /* Gary */
    if (hw_info.gary_rev) {
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.gary_rev);
    } else {
        strncpy(buffer, get_string(MSG_NA), sizeof(buffer) - 1);
    }
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_GARY_REV), buffer, 96);

    draw_label_value(HARDWARE_PANEL_X + 170, y,
                     get_string(MSG_IBURST), NULL, 64);
    y += 8;

    /* Card Slot */
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_CARD_SLOT), hw_info.card_slot_string, 96);

    draw_label_value(HARDWARE_PANEL_X + 170, y,
                     get_string(MSG_DBURST), NULL, 64);
    y += 8;

    /* Vert Hz */
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.vert_freq);
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_VERT_HZ), buffer, 96);

    draw_label_value(HARDWARE_PANEL_X + 170, y,
                     get_string(MSG_CBACK), NULL, 64);
    y += 8;

    /* Supply Hz */
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.supply_freq);
    draw_label_value(HARDWARE_PANEL_X + 4, y,
                     get_string(MSG_SUPPLY_HZ), buffer, 96);

    /* Draw cache status values */
    draw_cache_status();
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
 * Draw cache control buttons
 */
static void draw_cache_buttons(void)
{
    int i;
    for (i = 0; i < num_buttons; i++) {
        if (buttons[i].id >= BTN_ICACHE && buttons[i].id <= BTN_ALL) {
            draw_button(&buttons[i]);
        }
    }
}

/*
 * Draw only cache status values (right column of hardware panel)
 * Used for partial refresh when toggling cache settings
 */
static void draw_cache_status(void)
{
    struct RastPort *rp = app->rp;
    char buffer[64];
    WORD y;
    WORD value_x = HARDWARE_PANEL_X + 170 + 64;  /* After label */
    WORD value_w = 32;  /* Width for YES/NO/N/A */

    /* Calculate Y positions (matching draw_hardware_panel) */
    /* Base Y = HARDWARE_PANEL_Y + 24, then 10 lines down to ICache */
    y = HARDWARE_PANEL_Y + 24 + (10 * 8);  /* ICache row */

    /* Clear and redraw ICache value */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, value_x, y - 7, value_x + value_w, y + 1);
    snprintf(buffer, sizeof(buffer), "%s",
             hw_info.has_icache ?
                 (hw_info.icache_enabled ? get_string(MSG_YES) : get_string(MSG_NO)) :
                 get_string(MSG_NA));
    SetAPen(rp, COLOR_HIGHLIGHT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, value_x, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
    y += 8;

    /* DCache value */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, value_x, y - 7, value_x + value_w, y + 1);
    snprintf(buffer, sizeof(buffer), "%s",
             hw_info.has_dcache ?
                 (hw_info.dcache_enabled ? get_string(MSG_YES) : get_string(MSG_NO)) :
                 get_string(MSG_NA));
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, value_x, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
    y += 8;

    /* IBurst value */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, value_x, y - 7, value_x + value_w, y + 1);
    snprintf(buffer, sizeof(buffer), "%s",
             hw_info.has_iburst ?
                 (hw_info.iburst_enabled ? get_string(MSG_YES) : get_string(MSG_NO)) :
                 get_string(MSG_NA));
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, value_x, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
    y += 8;

    /* DBurst value */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, value_x, y - 7, value_x + value_w, y + 1);
    snprintf(buffer, sizeof(buffer), "%s",
             hw_info.has_dburst ?
                 (hw_info.dburst_enabled ? get_string(MSG_YES) : get_string(MSG_NO)) :
                 get_string(MSG_NA));
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, value_x, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
    y += 8;

    /* CBack value */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, value_x, y - 7, value_x + value_w, y + 1);
    snprintf(buffer, sizeof(buffer), "%s",
             hw_info.has_copyback ?
                 (hw_info.copyback_enabled ? get_string(MSG_YES) : get_string(MSG_NO)) :
                 get_string(MSG_NA));
    SetAPen(rp, COLOR_HIGHLIGHT);
    Move(rp, value_x, y);
    Text(rp, (CONST_STRPTR)buffer, strlen(buffer));
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
    switch (btn_id) {
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
            show_status_overlay(get_string(MSG_MEASURING_SPEED));
            run_benchmarks();
            hide_status_overlay();
            break;

        case BTN_PRINT:
            {
                char filename[MAX_FILENAME_LEN];
                strncpy(filename, DEFAULT_OUTPUT_FILE, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';

                if (show_filename_requester(
                        get_string(MSG_ENTER_FILENAME), filename, sizeof(filename))) {
                    export_to_file(filename);
                }
            }
            break;

        case BTN_SOFTWARE_CYCLE:
            app->software_type = (app->software_type + 1) % 3;
            app->software_scroll = 0;
            update_software_list();
            break;

        case BTN_SCALE_TOGGLE:
            app->bar_scale = (app->bar_scale == SCALE_SHRINK) ?
                             SCALE_EXPAND : SCALE_SHRINK;
            redraw_current_view();
            break;

        case BTN_ICACHE:
            toggle_icache();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_DCACHE:
            toggle_dcache();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_IBURST:
            toggle_iburst();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_DBURST:
            toggle_dburst();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_CBACK:
            toggle_copyback();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_ALL:
            toggle_icache();
            toggle_dcache();
            toggle_iburst();
            toggle_dburst();
            toggle_copyback();
            refresh_cache_status();
            draw_cache_status();
            break;

        case BTN_MEM_PREV:
            if (app->memory_region_index > 0) {
                app->memory_region_index--;
                redraw_current_view();
            }
            break;

        case BTN_MEM_NEXT:
            if (app->memory_region_index < (LONG)memory_regions.count - 1) {
                app->memory_region_index++;
                redraw_current_view();
            }
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
                                         &devices_list : &resources_list;
                if (app->software_scroll < (LONG)list->count - SOFTWARE_LIST_LINES) {
                    app->software_scroll++;
                    update_software_list();
                }
            }
            break;

        case BTN_SOFTWARE_SCROLLBAR:
            /* Scrollbar clicking is handled specially in handle_scrollbar_click */
            break;

        case BTN_MEM_EXIT:
        case BTN_DRV_EXIT:
        case BTN_BOARD_EXIT:
            switch_to_view(VIEW_MAIN);
            break;

        case BTN_SCSI_EXIT:
            switch_to_view(VIEW_DRIVES);
            break;

        case BTN_DRV_SCSI:
            if (app->selected_drive >= 0 &&
                app->selected_drive < (LONG)drive_list.count) {
                DriveInfo *drive = &drive_list.drives[app->selected_drive];
                scan_scsi_devices(drive->handler_name, drive->unit_number);
                switch_to_view(VIEW_SCSI);
            }
            break;

        case BTN_DRV_SPEED:
            if (app->selected_drive >= 0 &&
                app->selected_drive < (LONG)drive_list.count) {
                measure_drive_speed(app->selected_drive);
                redraw_current_view();
            }
            break;

        default:
            /* Check for drive selection buttons */
            if (btn_id >= BTN_DRV_DRIVE_BASE &&
                btn_id < BTN_DRV_DRIVE_BASE + MAX_DRIVES) {
                app->selected_drive = btn_id - BTN_DRV_DRIVE_BASE;
                redraw_current_view();
            }
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
            app->selected_drive = drive_list.count > 0 ? 0 : -1;
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

/*
 * Show status overlay (red background, centered message, no interaction)
 */
void show_status_overlay(const char *message)
{
    struct RastPort *rp = app->rp;
    WORD text_len = strlen(message);

    /* Dialog dimensions and position (centered) */
    WORD dialog_w = (text_len * 8) + 32;
    WORD dialog_h = 28;
    WORD dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
    WORD dialog_y = (app->screen_height - dialog_h) / 2;

    /* Hide mouse pointer with blank sprite */
    SetPointer(app->window, blank_pointer, 1, 1, 0, 0);

    /* Disable multitasking */
    Forbid();

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
    Move(rp, dialog_x + (dialog_w - text_len * 8) / 2, dialog_y + 16);
    Text(rp, (CONST_STRPTR)message, text_len);
}

/*
 * Hide status overlay and restore view
 */
void hide_status_overlay(void)
{
    /* Re-enable multitasking */
    Permit();

    /* Restore mouse pointer */
    ClearPointer(app->window);

    /* Redraw the main view to restore the area */
    redraw_current_view();
}

/*
 * Draw just the text field contents (for fast updates while typing)
 */
static void draw_requester_field(WORD field_x, WORD field_y, WORD field_w, WORD field_h,
                                 const char *filename, ULONG cursor_pos)
{
    struct RastPort *rp = app->rp;

    /* Clear field interior */
    SetAPen(rp, COLOR_BACKGROUND);
    RectFill(rp, field_x + 2, field_y + 2,
             field_x + field_w - 3, field_y + field_h - 3);

    /* Draw filename text */
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_BACKGROUND);
    Move(rp, field_x + 4, field_y + 10);
    Text(rp, (CONST_STRPTR)filename, strlen(filename));

    /* Draw cursor */
    {
        WORD cursor_x = field_x + 4 + cursor_pos * 8;
        SetAPen(rp, COLOR_TEXT);
        RectFill(rp, cursor_x, field_y + 2, cursor_x + 7, field_y + field_h - 3);
        /* Draw character at cursor position in inverse */
        if (filename[cursor_pos]) {
            SetAPen(rp, COLOR_BACKGROUND);
            SetBPen(rp, COLOR_TEXT);
            Move(rp, cursor_x, field_y + 10);
            Text(rp, (CONST_STRPTR)&filename[cursor_pos], 1);
        }
    }
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
    Move(rp, x + (w - strlen(title) * 8) / 2, y + 11);
    Text(rp, (CONST_STRPTR)title, strlen(title));

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
    Move(rp, x + 24 + (btn_w - 16) / 2, btn_y + 10);
    Text(rp, (CONST_STRPTR)"OK", 2);

    /* CANCEL button */
    SetAPen(rp, COLOR_PANEL_BG);
    RectFill(rp, x + w - 24 - btn_w, btn_y, x + w - 24 - 1, btn_y + btn_h - 1);
    draw_3d_box(x + w - 24 - btn_w, btn_y, btn_w, btn_h, FALSE);
    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, x + w - 24 - btn_w + (btn_w - 48) / 2, btn_y + 10);
    Text(rp, (CONST_STRPTR)"CANCEL", 6);
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
    Button ok_btn = { ok_x, btn_y, btn_w, btn_h, "OK", BTN_NONE, TRUE, FALSE };
    Button cancel_btn = { cancel_x, btn_y, btn_w, btn_h, "CANCEL", BTN_NONE, TRUE, FALSE };

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

            ReplyMsg((struct Message *)msg);

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

    /* Redraw the main view to restore the area */
    redraw_current_view();

    return result;
}
