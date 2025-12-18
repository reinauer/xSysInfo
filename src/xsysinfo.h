// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Amiga System Information Utility
 * Main header file with common definitions
 */

#ifndef XSYSINFO_H
#define XSYSINFO_H

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <dos/dos.h>

#include <stdio.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <graphics/text.h>

/* Default font settings */
#define DEFAULT_FONT_NAME   "topaz.font"
#define DEFAULT_FONT_HEIGHT 8

extern struct TextAttr Topaz8Font;

/* Program name */
#define XSYSINFO_NAME       "xSysInfo"
/* Version information is coming from Makefile */

/* Minimum library versions */
#define MIN_IDENTIFY_VERSION    13
#define MIN_INTUITION_VERSION   36
#define MIN_GRAPHICS_VERSION    37
#define MIN_DOS_VERSION         37
#define MIN_EXPANSION_VERSION   33
#define MIN_ICON_VERSION        36

/* Screen dimensions */
#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT_PAL   256
#define SCREEN_HEIGHT_NTSC  200
#define SCREEN_DEPTH        3       /* 8 colors */

/* Threshold for RTG detection */
#define RTG_WIDTH_THRESHOLD     640
#define RTG_HEIGHT_THRESHOLD    512

/* Number of colors in our palette */
#define NUM_COLORS          8

/* Pen lookup macro - maps logical color index to actual pen */
#define PEN(c) (app->pens[c])

/* Color definitions - use PEN() to map through allocated pens */
#define COLOR_BACKGROUND    PEN(0)
#define COLOR_PANEL_BG      PEN(1)
#define COLOR_TEXT          PEN(2)
#define COLOR_HIGHLIGHT     PEN(3)
#define COLOR_BAR_FILL      PEN(4)
#define COLOR_BAR_YOU       PEN(5)
#define COLOR_BUTTON_LIGHT  PEN(6)
#define COLOR_BUTTON_DARK   PEN(7)

/* Application state / view modes */
typedef enum {
    VIEW_MAIN,
    VIEW_MEMORY,
    VIEW_DRIVES,
    VIEW_BOARDS,
    VIEW_SCSI
} ViewMode;

/* Software list types */
typedef enum {
    SOFTWARE_LIBRARIES,
    SOFTWARE_DEVICES,
    SOFTWARE_RESOURCES
} SoftwareType;

/* Memory location classification */
typedef enum {
    LOC_ROM,
    LOC_32BIT_RAM,
    LOC_24BIT_RAM,
    LOC_CHIP_RAM,
    LOC_KICKSTART       /* Special: shows ROM size (256K/512K) */
} MemoryLocation;

/* Bar graph scale modes */
typedef enum {
    SCALE_SHRINK,       /* A4000 at 50%, cap faster systems */
    SCALE_EXPAND        /* Linear scale to fit all */
} BarScale;

/* Display mode (from tooltype or command line) */
typedef enum {
    DISPLAY_AUTO,       /* Auto-detect based on screen resolution */
    DISPLAY_WINDOW,     /* Force window on Workbench */
    DISPLAY_SCREEN      /* Force custom screen */
} DisplayMode;

/* Global application context */
typedef struct {
    /* Display */
    struct Screen *screen;          /* Custom screen (or NULL if using WB) */
    struct Window *window;          /* Main window */
    struct RastPort *rp;            /* RastPort for drawing */
    struct TextFont *tf;            /* Current font */
    BOOL use_custom_screen;         /* TRUE if we opened our own screen */
    BOOL is_pal;                    /* TRUE if PAL system */
    UWORD screen_height;            /* Actual screen height (200 or 256) */
    DisplayMode display_mode;       /* Requested display mode */

    /* Pen allocation for Workbench window mode */
    WORD pens[NUM_COLORS];          /* Mapped pen numbers for each color */
    BOOL pens_allocated;            /* TRUE if pens need ReleasePen() */

    /* Libraries */
    struct Library *IdentifyBase;

    /* Current view */
    ViewMode current_view;

    /* Main view state */
    SoftwareType software_type;     /* Which list is shown */
    LONG software_scroll;           /* Scroll offset */
    BarScale bar_scale;             /* Current bar graph scale */
    BOOL benchmarks_run;            /* Have benchmarks been executed? */
    BOOL scrollbar_dragging;        /* TRUE while dragging scrollbar */
    WORD pressed_button;            /* Currently pressed button ID, or -1 */

    /* Memory view state */
    LONG memory_region_index;       /* Currently displayed region */
    LONG memory_region_count;       /* Total regions */

    /* Drives view state */
    LONG selected_drive;            /* Currently selected drive */
    LONG drive_count;               /* Total drives */

    /* Boards view state */
    LONG board_scroll;              /* Scroll offset */
    LONG board_count;               /* Total boards */

    /* Exit flag */
    BOOL running;
} AppContext;

/* Global context (defined in main.c) */
extern AppContext *app;
extern struct ExecBase *SysBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
extern struct Library *IdentifyBase;

/* Function prototypes - initialization */
BOOL init_libraries(void);
void cleanup_libraries(void);
BOOL init_display(void);
void cleanup_display(void);

/* Utility functions */
MemoryLocation determine_mem_location(APTR addr);
const char *get_location_string(MemoryLocation loc);
void format_size(ULONG bytes, char *buffer, ULONG bufsize);
void format_hex(ULONG value, char *buffer, ULONG bufsize);

void format_scaled(char *buffer, size_t size, ULONG value_x100, BOOL round);

#endif /* XSYSINFO_H */
