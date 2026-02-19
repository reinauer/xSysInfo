// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - GUI header
 */

#ifndef GUI_H
#define GUI_H

#include "xsysinfo.h"
#include "drives.h"

/* Button IDs */
typedef enum {
    BTN_NONE = 0,
    BTN_QUIT,
    BTN_MEMORY,
    BTN_DRIVES,
    BTN_BOARDS,
    BTN_SPEED,
    BTN_PRINT,

    /* Main view toggles */
    BTN_SOFTWARE_CYCLE,     /* Libraries/Devices/Resources cycle */
    BTN_HARDWARE_CYCLE,     /* Hardwareinfo cycle */
    BTN_SOFTWARE_UP,        /* Software list scroll up */
    BTN_SOFTWARE_DOWN,      /* Software list scroll down */
    BTN_SOFTWARE_SCROLLBAR, /* Software list scroll bar */
    BTN_SCALE_TOGGLE,       /* Expand/Shrink */


    /* Cache toggle buttons (inline in hardware panel) */
    BTN_ICACHE,
    BTN_DCACHE,
    BTN_IBURST,
    BTN_DBURST,
    BTN_CBACK,
    BTN_SUPER_SCALAR,

    /* Memory view buttons */
    BTN_MEM_PREV,
    BTN_MEM_COUNTER,    /* Display-only counter between prev/next */
    BTN_MEM_NEXT,
    BTN_MEM_SPEED,
    BTN_MEM_EXIT,

    /* Drives view buttons */
    BTN_DRV_EXIT,
    BTN_DRV_SCSI,
    BTN_DRV_SPEED,

    /* Boards view button */
    BTN_BOARD_EXIT,

    /* SCSI view button */
    BTN_SCSI_EXIT,

    /* Drive selection buttons - MUST be last as they use sequential IDs */
    BTN_DRV_DRIVE_BASE,

    BTN_COUNT = BTN_DRV_DRIVE_BASE + MAX_DRIVES
} ButtonID;

/* Button definition */
typedef struct {
    WORD x, y;              /* Position */
    WORD width, height;     /* Size */
    const char *label;      /* Button text */
    ButtonID id;            /* Button identifier */
    BOOL enabled;           /* Can be clicked */
    BOOL pressed;           /* Currently pressed */
} Button;

/* Layout constants for main view */
#define HEADER_HEIGHT       23
#define PANEL_MARGIN        4
#define TEXT_LINE_HEIGHT    8
#define BUTTON_HEIGHT       11
#define BUTTON_MARGIN       1

/* Panel positions (main view) */
#define SOFTWARE_PANEL_X    0
#define SOFTWARE_PANEL_Y    24
#define SOFTWARE_PANEL_W    366
#define SOFTWARE_PANEL_H    77

#define SPEED_PANEL_X       0
#define SPEED_PANEL_Y       102
#define SPEED_PANEL_W       366
#define SPEED_PANEL_H       98

#define HARDWARE_PANEL_X    368
#define HARDWARE_PANEL_Y    24
#define HARDWARE_PANEL_W    272
#define HARDWARE_PANEL_H    176

#define CACHE_PANEL_X       480
#define CACHE_PANEL_Y       136
#define CACHE_PANEL_W       156
#define CACHE_PANEL_H       28

/* Software list display */
#define SOFTWARE_LIST_LINES 7
#define SOFTWARE_NAME_WIDTH 100
#define SOFTWARE_LOC_WIDTH  60
#define SOFTWARE_ADDR_WIDTH 80
#define SOFTWARE_VER_WIDTH  60

/* Speed comparison bar */
#define SPEED_BAR_MAX_WIDTH 180
#define SPEED_BAR_HEIGHT    6
#define SPEED_BAR_X         200
#define SPEED_BAR_Y_START   122

/* Function prototypes */

/* Main drawing functions */
void draw_main_view(void);
void draw_memory_view(void);
void draw_drives_view(void);
void draw_boards_view(void);

/* Redraw current view */
void redraw_current_view(void);

/* Panel drawing helpers */
void draw_panel(WORD x, WORD y, WORD w, WORD h, const char *title);
void draw_button(Button *btn);
void draw_cycle_button(Button *btn);
void draw_3d_box(WORD x, WORD y, WORD w, WORD h, BOOL recessed);

/* Text drawing helpers */
void TightText(struct RastPort *rp, int x, int y, CONST_STRPTR str, int charGap, int spaceWidth);
void draw_text(WORD x, WORD y, const char *text, UBYTE color);
void draw_text_right(WORD x, WORD y, WORD width, const char *text, UBYTE color);
void draw_label_value(WORD x, WORD y, const char *label, const char *value, WORD offset);

/* Bar graph drawing */
void draw_speed_bars(void);
void draw_single_bar(WORD x, WORD y, ULONG value, ULONG max_value, WORD color);

/* Scroll bar drawing */
void draw_scroll_arrow(WORD x, WORD y, WORD w, WORD h, BOOL up, BOOL pressed);
void draw_scroll_bar(WORD x, WORD y, WORD w, WORD h, ULONG pos, ULONG total, ULONG visible);

/* Event handling */
ButtonID handle_click(WORD mx, WORD my);
void handle_button_press(ButtonID btn);
void handle_scrollbar_click(WORD mx, WORD my);

/* Button state management */
void init_buttons(void);
void update_button_states(void);
void add_button(WORD x, WORD y, WORD w, WORD h, const char *label, ButtonID id, BOOL enabled);
Button *find_button(ButtonID id);
void set_button_pressed(ButtonID id, BOOL pressed);
void redraw_button(ButtonID id);

/* View switching */
void switch_to_view(ViewMode view);

/* Overlay requester for filename input */
BOOL show_filename_requester(const char *title, char *filename, ULONG filename_size);

/* Status overlay (no input, just display) */
void show_status_overlay(const char *message);
void hide_status_overlay(void);

/* View Controller Prototypes */
void main_view_update_buttons(void);
void main_view_handle_button(ButtonID id);

void memory_view_update_buttons(void);
void memory_view_handle_button(ButtonID id);

void drives_view_update_buttons(void);
void drives_view_handle_button(ButtonID id);

void boards_view_update_buttons(void);
void boards_view_handle_button(ButtonID id);

void scsi_view_update_buttons(void);
void scsi_view_handle_button(ButtonID id);

#endif /* GUI_H */
