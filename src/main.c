// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Main entry point and display management
 */

#include <string.h>

#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <libraries/identify.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/identify.h>
#include <proto/icon.h>

#include "xsysinfo.h"
#include "gui.h"
#include "hardware.h"
#include "software.h"
#include "memory.h"
#include "boards.h"
#include "drives.h"
#include "benchmark.h"
#include "locale_str.h"
#include "debug.h"

/* Amiga version string for the Version command */
__attribute__((used))
static const char version_string[] = "$VER: " XSYSINFO_NAME " " XSYSINFO_VERSION " (" XSYSINFO_DATE ")";

/* Global library bases */
extern struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *IdentifyBase = NULL;
struct Library *IconBase = NULL;

/* Workbench startup message (if started from WB) */
static struct WBStartup *wb_startup = NULL;

/* Global debug flag */
BOOL g_debug_enabled = FALSE;

/* Global application context */
AppContext app_context;
struct TextAttr Topaz8Font = {
    (STRPTR)DEFAULT_FONT_NAME,
    DEFAULT_FONT_HEIGHT,
    FS_NORMAL,
    FPF_ROMFONT
};
AppContext *app = &app_context;

/* Command line argument template */
#define TEMPLATE "DEBUG/S"

/* Argument array indices */
enum {
    ARG_DEBUG,
    ARG_COUNT
};

/* Color palette matching original SysInfo */
static const UWORD palette[8] = {
    0x0AAA,     /* 0: Gray screen background */
    0x0AAA,     /* 1: Gray panel background */
    0x0000,     /* 2: Black text */
    0x0FFF,     /* 3: White highlight */
    0x0068,     /* 4: Blue bar fill */
    0x0F00,     /* 5: Red "You" bar */
    0x0DDD,     /* 6: Light (3D button top) */
    0x0444,     /* 7: Dark (3D button shadow) */
};

/* Default pens array for SA_Pens (use system defaults) */
static const UWORD default_pens[] = { (UWORD)~0 };

/* Forward declarations */
static BOOL open_libraries(void);
static void close_libraries(void);
static BOOL open_display(void);
static void close_display(void);
static void main_loop(void);
static void set_palette(void);
static void allocate_pens(void);
static void release_pens(void);
static BOOL is_rtg_mode(struct Screen *screen);
static void parse_tooltypes(void);

/*
 * Parse command line arguments
 * Returns TRUE on success, FALSE on failure
 */
static BOOL parse_args(void)
{
    struct RDArgs *rdargs;
    LONG args[ARG_COUNT] = { 0 };

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL);
    if (!rdargs) {
        /* ReadArgs failed - but we'll continue with defaults */
        return TRUE;
    }

    /* Check for DEBUG switch */
    if (args[ARG_DEBUG]) {
        g_debug_enabled = TRUE;
    }

    FreeArgs(rdargs);
    return TRUE;
}

/*
 * Parse icon tooltypes when started from Workbench
 */
static void parse_tooltypes(void)
{
    struct DiskObject *dobj;
    char **tooltypes;
    char *value;
    BPTR old_dir;

    if (!wb_startup || !IconBase) {
        return;
    }

    /* Change to the program's directory */
    old_dir = CurrentDir(wb_startup->sm_ArgList[0].wa_Lock);

    /* Get the program's icon */
    dobj = GetDiskObject(wb_startup->sm_ArgList[0].wa_Name);
    if (dobj) {
        tooltypes = (char **)dobj->do_ToolTypes;

        /* Check for DISPLAY tooltype */
        value = (char *)FindToolType((CONST_STRPTR *)tooltypes, (CONST_STRPTR)"DISPLAY");
        if (value) {
            if (MatchToolValue((CONST_STRPTR)value, (CONST_STRPTR)"WINDOW")) {
                app->display_mode = DISPLAY_WINDOW;
            } else if (MatchToolValue((CONST_STRPTR)value, (CONST_STRPTR)"SCREEN")) {
                app->display_mode = DISPLAY_SCREEN;
            } else if (MatchToolValue((CONST_STRPTR)value, (CONST_STRPTR)"AUTO")) {
                app->display_mode = DISPLAY_AUTO;
            }
        }

        /* Check for DEBUG tooltype */
        if (FindToolType((CONST_STRPTR *)tooltypes, (CONST_STRPTR)"DEBUG")) {
            g_debug_enabled = TRUE;
        }

        FreeDiskObject(dobj);
    }

    CurrentDir(old_dir);
}

/*
 * Main entry point
 */
int main(int argc, char **argv)
{
    int ret = RETURN_OK;

    /* Check if started from Workbench */
    if (argc == 0) {
        /* Started from Workbench - argv is actually a WBStartup pointer */
        wb_startup = (struct WBStartup *)argv;
    } else {
        /* Started from CLI - parse command line arguments */
        parse_args();
    }

    debug(XSYSINFO_NAME ": Starting...\n");

    /* Initialize application context */
    memset(app, 0, sizeof(AppContext));
    app->current_view = VIEW_MAIN;
    app->software_type = SOFTWARE_LIBRARIES;
    app->bar_scale = SCALE_SHRINK;
    app->running = TRUE;
    app->pressed_button = -1;

    debug(XSYSINFO_NAME ": Initializing locale...\n");
    /* Initialize locale */
    init_locale();

    debug(XSYSINFO_NAME ": Opening libraries...\n");
    /* Open required libraries */
    if (!open_libraries()) {
        ret = RETURN_FAIL;
        goto cleanup;
    }

    /* Parse tooltypes if started from Workbench */
    if (wb_startup) {
        parse_tooltypes();
    }

    debug(XSYSINFO_NAME ": Detecting hardware...\n");
    /* Detect hardware */
    if (!detect_hardware()) {
        Printf((CONST_STRPTR)"Failed to detect hardware\n");
        ret = RETURN_FAIL;
        goto cleanup;
    }

    debug(XSYSINFO_NAME ": Enumerating software...\n");
    /* Enumerate system software */
    enumerate_all_software();

    debug(XSYSINFO_NAME ": Enumerating memory...\n");
    /* Enumerate memory regions */
    enumerate_memory_regions();

    debug(XSYSINFO_NAME ": Enumerating boards...\n");
    /* Enumerate expansion boards */
    enumerate_boards();

    debug(XSYSINFO_NAME ": Enumerating drives...\n");
    /* Enumerate drives */
    enumerate_drives();

    debug(XSYSINFO_NAME ": Opening display...\n");
    /* Open display (screen or window) */
    if (!open_display()) {
        Printf((CONST_STRPTR)"%s\n", (LONG)get_string(MSG_ERR_NO_WINDOW));
        ret = RETURN_FAIL;
        goto cleanup;
    }

    /* Initialize GUI buttons */
    init_buttons();

    /* Initialize benchmark timer */
    if (!init_timer()) {
        Printf((CONST_STRPTR)"Failed to initialize timer\n");
        ret = RETURN_FAIL;
        goto cleanup;
    }

    /* Draw initial view */
    redraw_current_view();

    /* Main event loop */
    main_loop();

cleanup:
    cleanup_timer();
    close_display();
    close_libraries();
    cleanup_locale();

    return ret;
}

/*
 * Open required libraries
 */
static BOOL open_libraries(void)
{
    /* exec.library is always available via SysBase */
    // SysBase = *(struct ExecBase **)4;

    /* Open intuition.library */
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", MIN_INTUITION_VERSION);
    if (!IntuitionBase) {
        Printf((CONST_STRPTR)"Could not open intuition.library v%d\n",
               MIN_INTUITION_VERSION);
        return FALSE;
    }

    /* Open graphics.library */
    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", MIN_GRAPHICS_VERSION);
    if (!GfxBase) {
        Printf((CONST_STRPTR)"Could not open graphics.library v%d\n",
               MIN_GRAPHICS_VERSION);
        return FALSE;
    }

    /* Open identify.library - required */
    IdentifyBase = OpenLibrary((CONST_STRPTR)"identify.library", MIN_IDENTIFY_VERSION);
    if (!IdentifyBase) {
        Printf((CONST_STRPTR)"%s\n", (LONG)get_string(MSG_ERR_NO_IDENTIFY));
        return FALSE;
    }

    app->IdentifyBase = IdentifyBase;

    /* Open icon.library - optional, for reading tooltypes */
    {
        struct Process *proc = (struct Process *)FindTask(NULL);
        APTR old_window = proc->pr_WindowPtr;
        proc->pr_WindowPtr = (APTR)-1; /* Suppress system requesters */

        IconBase = OpenLibrary((CONST_STRPTR)"icon.library", MIN_ICON_VERSION);

        proc->pr_WindowPtr = old_window;
    }
    /* Not a failure if icon.library can't be opened */

    return TRUE;
}

/*
 * Close libraries
 */
static void close_libraries(void)
{
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }

    if (IdentifyBase) {
        CloseLibrary(IdentifyBase);
        IdentifyBase = NULL;
    }

    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }

    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

/*
 * Check if a screen is using RTG (high resolution) mode
 */
static BOOL is_rtg_mode(struct Screen *screen)
{
    if (!screen) return FALSE;

    /* Check if resolution exceeds native chipset limits */
    if (screen->Width > RTG_WIDTH_THRESHOLD ||
        screen->Height > RTG_HEIGHT_THRESHOLD) {
        return TRUE;
    }

    /* Could also check the ModeID for RTG modes, but resolution check
     * is sufficient for our purposes */

    return FALSE;
}

/*
 * Open display - either a window on Workbench or a custom screen
 */
static BOOL open_display(void)
{
    struct Screen *wb_screen;
    BOOL use_window = FALSE;

    /* Check display mode setting from tooltypes */
    if (app->display_mode == DISPLAY_WINDOW) {
        use_window = TRUE;
    } else if (app->display_mode == DISPLAY_SCREEN) {
        use_window = FALSE;
    } else {
        /* AUTO mode - detect based on RTG */
        /* Lock Workbench screen to check its mode */
        wb_screen = LockPubScreen((CONST_STRPTR)"Workbench");
        if (!wb_screen) {
            wb_screen = LockPubScreen(NULL);  /* Default public screen */
        }

        if (wb_screen) {
            /* Check if Workbench is in RTG mode */
            use_window = is_rtg_mode(wb_screen);
            UnlockPubScreen(NULL, wb_screen);
        }
    }

    /* Determine if system is PAL or NTSC */
    app->is_pal = (GfxBase->DisplayFlags & PAL) ? TRUE : FALSE;

    if (use_window) {
        /* Open window on Workbench */
        app->use_custom_screen = FALSE;

        app->window = OpenWindowTags(NULL,
            WA_Title, (ULONG)XSYSINFO_NAME " " XSYSINFO_VERSION,
            WA_InnerWidth, SCREEN_WIDTH,
            WA_InnerHeight, SCREEN_HEIGHT_NTSC + 10,
            WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS |
                      IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                      IDCMP_MOUSEMOVE | IDCMP_RAWKEY,
            WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_ACTIVATE | WFLG_SMART_REFRESH | WFLG_GIMMEZEROZERO |
                      WFLG_REPORTMOUSE,
            WA_PubScreenName, (ULONG)"Workbench",
            TAG_DONE);

        if (!app->window) {
            return FALSE;
        }

        app->rp = app->window->RPort;
        app->screen = app->window->WScreen;
        app->screen_height = app->screen->Height;

        /* If default screen font is larger than Topaz8, switch to Topaz8 */
        if(app->window->IFont->tf_YSize > Topaz8Font.ta_YSize)
        {
            app->tf = OpenFont(&Topaz8Font);
            if(app->tf)
            {
                SetFont(app->rp, app->tf);
            }
        }
    } else {
        /* Open custom screen */
        app->use_custom_screen = TRUE;

        app->screen_height = app->is_pal ? SCREEN_HEIGHT_PAL : SCREEN_HEIGHT_NTSC;

        app->screen = OpenScreenTags(NULL,
            SA_Width, SCREEN_WIDTH,
            SA_Height, app->screen_height,
            SA_Depth, SCREEN_DEPTH,
            SA_Title, (ULONG)XSYSINFO_NAME " " XSYSINFO_VERSION,
            SA_Type, CUSTOMSCREEN,
            SA_Font, (ULONG)&Topaz8Font,
            SA_DisplayID, HIRES_KEY,
            SA_Pens, (ULONG)default_pens,
            SA_ShowTitle, FALSE,
            TAG_DONE);

        if (!app->screen) {
            Printf((CONST_STRPTR)"%s\n", (LONG)get_string(MSG_ERR_NO_SCREEN));
            return FALSE;
        }

        /* Set our palette */
        set_palette();

        /* Open borderless window on our screen */
        app->window = OpenWindowTags(NULL,
            WA_CustomScreen, (ULONG)app->screen,
            WA_Left, 0,
            WA_Top, 0,
            WA_Width, SCREEN_WIDTH,
            WA_Height, app->screen_height,
            WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW |
                      IDCMP_MOUSEMOVE | IDCMP_RAWKEY,
            WA_Flags, WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_BACKDROP |
                      WFLG_RMBTRAP | WFLG_SMART_REFRESH | WFLG_REPORTMOUSE,
            TAG_DONE);

        if (!app->window) {
            CloseScreen(app->screen);
            app->screen = NULL;
            return FALSE;
        }

        app->rp = app->window->RPort;
    }

    /* Allocate/map pens for drawing */
    allocate_pens();

    return TRUE;
}

/*
 * Close display
 */
static void close_display(void)
{
    /* Release any allocated pens before closing */
    release_pens();

    if (app->window) {
        CloseWindow(app->window);
        app->window = NULL;
    }

    if (app->tf) {
        CloseFont(app->tf);
        app->tf = NULL;
    }

    if (app->use_custom_screen && app->screen) {
        CloseScreen(app->screen);
        app->screen = NULL;
    }

    app->rp = NULL;
}

/*
 * Set color palette for custom screen
 */
static void set_palette(void)
{
    UWORD i;

    if (!app->screen) return;

    for (i = 0; i < 8; i++) {
        SetRGB4(&app->screen->ViewPort,
                i,
                (palette[i] >> 8) & 0xF,
                (palette[i] >> 4) & 0xF,
                palette[i] & 0xF);
    }
}

/*
 * Allocate pens for drawing
 * On custom screen: use direct pen indices 0-7
 * On Workbench: obtain best matching pens from screen's colormap
 */
static void allocate_pens(void)
{
    UWORD i;
    struct ColorMap *cm;

    app->pens_allocated = FALSE;

    if (app->use_custom_screen) {
        /* Custom screen: we control the palette, use direct indices */
        for (i = 0; i < NUM_COLORS; i++) {
            app->pens[i] = i;
        }
        return;
    }

    /* Workbench window mode: need to obtain matching pens */
    cm = app->screen->ViewPort.ColorMap;

    if (GfxBase->LibNode.lib_Version >= 39) {
        /* OS 3.0+: Use ObtainBestPenA for best color match with allocation */
        for (i = 0; i < NUM_COLORS; i++) {
            /* Convert 4-bit RGB to 32-bit RGB for ObtainBestPen */
            ULONG r = ((palette[i] >> 8) & 0xF) * 0x11111111;
            ULONG g = ((palette[i] >> 4) & 0xF) * 0x11111111;
            ULONG b = (palette[i] & 0xF) * 0x11111111;

            app->pens[i] = ObtainBestPenA(cm, r, g, b, NULL);
            if (app->pens[i] == -1) {
                /* Fallback to pen 1 if allocation fails */
                app->pens[i] = 1;
            }
        }
        app->pens_allocated = TRUE;
    } else {
        /* OS 2.0 (v37-v38): Use FindColor for best match without allocation */
        for (i = 0; i < NUM_COLORS; i++) {
            /* Convert 4-bit RGB to 32-bit RGB for FindColor */
            ULONG r = ((palette[i] >> 8) & 0xF) * 0x11111111;
            ULONG g = ((palette[i] >> 4) & 0xF) * 0x11111111;
            ULONG b = (palette[i] & 0xF) * 0x11111111;

            app->pens[i] = FindColor(cm, r, g, b, -1);
        }
    }
}

/*
 * Release allocated pens
 */
static void release_pens(void)
{
    UWORD i;
    struct ColorMap *cm;

    if (!app->pens_allocated || !app->screen) {
        return;
    }

    cm = app->screen->ViewPort.ColorMap;

    /* Only release if we used ObtainBestPenA (OS 3.0+) */
    if (GfxBase->LibNode.lib_Version >= 39) {
        for (i = 0; i < NUM_COLORS; i++) {
            if (app->pens[i] != -1) {
                ReleasePen(cm, app->pens[i]);
            }
        }
    }

    app->pens_allocated = FALSE;
}

/*
 * Main event loop
 */
static void main_loop(void)
{
    struct IntuiMessage *msg;
    ULONG signals;
    ULONG win_signal;

    win_signal = 1L << app->window->UserPort->mp_SigBit;

    while (app->running) {
        signals = Wait(win_signal | SIGBREAKF_CTRL_C);

        /* Check for break */
        if (signals & SIGBREAKF_CTRL_C) {
            app->running = FALSE;
            break;
        }

        /* Process window messages */
        while ((msg = (struct IntuiMessage *)
                GetMsg(app->window->UserPort)) != NULL) {

            ULONG class = msg->Class;
            UWORD code = msg->Code;
            WORD mx = msg->MouseX;
            WORD my = msg->MouseY;

            /* In windowed mode, adjust for window decorations */
            if (!app->use_custom_screen) {
                mx -= app->window->BorderLeft;
                my -= app->window->BorderTop;
            }

            ReplyMsg((struct Message *)msg);

            switch (class) {
                case IDCMP_CLOSEWINDOW:
                    app->running = FALSE;
                    break;

                case IDCMP_MOUSEBUTTONS:
                    if (code == SELECTDOWN) {
                        ButtonID btn = handle_click(mx, my);
                        if (btn != BTN_NONE) {
                            if (btn == BTN_SOFTWARE_SCROLLBAR) {
                                app->scrollbar_dragging = TRUE;
                                handle_scrollbar_click(mx, my);
                            } else {
                                /* Set button as pressed and redraw */
                                app->pressed_button = btn;
                                set_button_pressed(btn, TRUE);
                                redraw_button(btn);
                            }
                        }
                    } else if (code == SELECTUP) {
                        app->scrollbar_dragging = FALSE;
                        /* Release pressed button */
                        if (app->pressed_button != -1) {
                            ButtonID btn = (ButtonID)app->pressed_button;
                            set_button_pressed(btn, FALSE);
                            redraw_button(btn);
                            /* Check if mouse is still over the button */
                            if (handle_click(mx, my) == btn) {
                                handle_button_press(btn);
                            }
                            app->pressed_button = -1;
                        }
                    }
                    break;

                case IDCMP_MOUSEMOVE:
                    if (app->scrollbar_dragging) {
                        handle_scrollbar_click(mx, my);
                    }
                    break;

                case IDCMP_VANILLAKEY:
                    /* Handle keyboard shortcuts */
                    switch (code) {
                        case 'q':
                        case 'Q':
                        case 0x1B:  /* Escape */
                            if (app->current_view == VIEW_MAIN) {
                                app->running = FALSE;
                            } else {
                                switch_to_view(VIEW_MAIN);
                            }
                            break;
                        case 'm':
                        case 'M':
                            if (app->current_view == VIEW_MAIN) {
                                switch_to_view(VIEW_MEMORY);
                            }
                            break;
                        case 'd':
                        case 'D':
                            if (app->current_view == VIEW_MAIN) {
                                switch_to_view(VIEW_DRIVES);
                            }
                            break;
                        case 'b':
                        case 'B':
                            if (app->current_view == VIEW_MAIN) {
                                switch_to_view(VIEW_BOARDS);
                            }
                            break;
                        case 's':
                        case 'S':
                            if (app->current_view == VIEW_MAIN) {
                                run_benchmarks();
                                redraw_current_view();
                            }
                            break;
                        case 'p':
                        case 'P':
                            if (app->current_view == VIEW_MAIN) {
                                handle_button_press(BTN_PRINT);
                            }
                            break;
                    }
                    break;

                case IDCMP_REFRESHWINDOW:
                    BeginRefresh(app->window);
                    redraw_current_view();
                    EndRefresh(app->window, TRUE);
                    break;
            }
        }
    }
}

/*
 * Utility: Determine memory location classification
 */
MemoryLocation determine_mem_location(APTR addr)
{
    ULONG address = (ULONG)addr;

    /* ROM area: $F80000-$FFFFFF (256K) or $E00000-$E7FFFF (512K extended) */
    if ((address >= 0xF80000 && address <= 0xFFFFFF) ||
		    (address >= 0xE0000 && address < 0xE80000)) {
        return LOC_ROM;
    }

    /* Chip RAM: $000000-$1FFFFF (2MB max) */
    if (address < 0x200000) {
        return LOC_CHIP_RAM;
    }

    /* 24-bit addressable fast RAM: up to $00FFFFFF */
    if (address < 0x01000000) {
        return LOC_24BIT_RAM;
    }

    /* 32-bit RAM: above $01000000 */
    return LOC_32BIT_RAM;
}

/*
 * Utility: Get location string
 */
const char *get_location_string(MemoryLocation loc)
{
    static char kickstart_size_str[16];

    switch (loc) {
        case LOC_ROM:       return "ROM";
        case LOC_CHIP_RAM:  return "CHIP RAM";
        case LOC_24BIT_RAM: return "24BitRAM";
        case LOC_32BIT_RAM: return "32BitRAM";
        case LOC_KICKSTART:
            /* Return ROM size string (e.g., "256K" or "512K") */
            /* kickstart_size is in KB from identify.library */
            if (hw_info.kickstart_size >= 1024) {
                /* Size is in bytes, convert to KB */
                snprintf(kickstart_size_str, sizeof(kickstart_size_str),
                         " (%luK) ", (unsigned long)(hw_info.kickstart_size / 1024));
            } else {
                /* Size is already in KB */
                snprintf(kickstart_size_str, sizeof(kickstart_size_str),
                         " (%luK) ", (unsigned long)hw_info.kickstart_size);
            }
            return kickstart_size_str;
        default:            return " (\?\?\?) ";
    }
}

/*
 * Utility: Format byte size to human-readable string with fractions
 * Uses fixed-point math (x100) via format_scaled
 */
void format_size(ULONG bytes, char *buffer, ULONG bufsize)
{
    char num_buf[32];
    ULONG scaled;

    if (bytes >= 1024 * 1024 * 1024) {
        scaled = (bytes / (1024 * 1024 * 1024)) * 100 +
                 ((bytes % (1024 * 1024 * 1024)) * 100) / (1024 * 1024 * 1024);
        format_scaled(num_buf, sizeof(num_buf), scaled, TRUE);
        snprintf(buffer, bufsize, "%sG", num_buf);
    } else if (bytes >= 1024 * 1024) {
        scaled = (bytes / (1024 * 1024)) * 100 +
                 ((bytes % (1024 * 1024)) * 100) / (1024 * 1024);
        format_scaled(num_buf, sizeof(num_buf), scaled, TRUE);
        snprintf(buffer, bufsize, "%sM", num_buf);
    } else if (bytes >= 1024) {
        scaled = (bytes / 1024) * 100 + ((bytes % 1024) * 100) / 1024;
        format_scaled(num_buf, sizeof(num_buf), scaled, TRUE);
        snprintf(buffer, bufsize, "%sK", num_buf);
    } else {
        snprintf(buffer, bufsize, "%lu", (unsigned long)bytes);
    }
}

/*
 * Utility: Format hex value
 */
void format_hex(ULONG value, char *buffer, ULONG bufsize)
{
    snprintf(buffer, bufsize, "$%08lX", (unsigned long)value);
}
