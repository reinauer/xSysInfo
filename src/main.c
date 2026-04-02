// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Main entry point and display management
 */

#include <stdio.h>
#include <string.h>

#include <exec/execbase.h>
#include <exec/memory.h>
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
extern struct DosLibrary *DOSBase;

/* Workbench startup message (if started from WB) */
static struct WBStartup *wb_startup = NULL;

/* Global debug flag */
BOOL g_debug_enabled = FALSE;

/* Text-only mode (no GUI, print results to stdout) */
static BOOL g_text_mode = FALSE;

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

/* Forward declarations */
static BOOL open_libraries(void);
static void close_libraries(void);
static BOOL open_display(void);
static void close_display(void);
static void main_loop(void);
static void set_palette(void);
static void allocate_pens(void);
static void release_pens(void);
static void parse_tooltypes(void);

/*
 * Case-insensitive string compare (portable, no OS dependency)
 */
static int xstricmp(const char *o1, const char *o2)
{
    if (o1 == NULL)
        return 1;
    if (o2 == NULL)
        return -1;
    int i = 0;
    char a, b;
    while (o1[i] != 0 && o2[i] != 0) {
        a = o1[i];
        if (a >= 'A' && a <= 'Z')
            a += 0x20;
        b = o2[i];
        if (b >= 'A' && b <= 'Z')
            b += 0x20;
        if (a != b) {
            return a > b ? -1 : 1;
        }
        i++;
    }
    if (o1[i] == 0 && o2[i] == 0)
        return 0;
    if (o1[i] == 0)
        return 1;
    if (o2[i] == 0)
        return -1;

    return 0;
}

/*
 * Parse command line arguments
 * Returns TRUE on success, FALSE on failure
 */
static BOOL parse_args(int argc, char **argv)
{
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (xstricmp(argv[i], "debug") == 0)
                g_debug_enabled = TRUE;
            else if (xstricmp(argv[i], "text") == 0)
                g_text_mode = TRUE;
        }
    }
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

        /* Check for TEXT tooltype */
        if (FindToolType((CONST_STRPTR *)tooltypes, (CONST_STRPTR)"TEXT")) {
            g_text_mode = TRUE;
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
    debug(XSYSINFO_NAME ": Checking start...\n");

    /* Check if started from Workbench */
    if (argc == 0) {
        /* Started from Workbench - argv is actually a WBStartup pointer */
        wb_startup = (struct WBStartup *)argv;
    } else {
        /* Started from CLI - parse command line arguments */
        parse_args(argc, argv);
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

    debug(XSYSINFO_NAME ": Init timer...\n");
    /* Initialize benchmark timer */
    if (!init_timer()) {
        Printf((CONST_STRPTR)"Failed to initialize timer\n");
        ret = RETURN_FAIL;
        goto cleanup;
    }

    if (!g_text_mode) {
        debug(XSYSINFO_NAME ": Opening display...\n");
        if (!open_display()) {
            Printf((CONST_STRPTR)"%s\n", get_string(MSG_ERR_NO_WINDOW));
            ret = RETURN_FAIL;
            goto cleanup;
        }

        debug(XSYSINFO_NAME ": Init buttons...\n");
        init_buttons();

        debug(XSYSINFO_NAME ": Draw screen...\n");
        redraw_current_view();

        debug(XSYSINFO_NAME ": Start main loop...\n");
        main_loop();
    } else {
        char buffer[16];

        run_benchmarks();

        printf("CPU: %s MHz: ", hw_info.cpu_string);
        if (hw_info.cpu_mhz > 0) {
            format_scaled(buffer, sizeof(buffer), hw_info.cpu_mhz, TRUE);
            printf("%s\n", buffer);
        } else {
            printf("%s\n", get_string(MSG_NA));
        }

        printf("MMU: %s enabled: %s\n", hw_info.mmu_string,
               hw_info.mmu_enabled ? get_string(MSG_YES) : get_string(MSG_NO));

        printf("FPU: %s MHz: ", hw_info.fpu_string);
        if (hw_info.fpu_mhz > 0) {
            format_scaled(buffer, sizeof(buffer), hw_info.fpu_mhz, TRUE);
            printf("%s\n", buffer);
        } else {
            printf("%s\n", get_string(MSG_NA));
        }

        printf("Dhrystones: ");
        if (bench_results.benchmarks_valid)
            printf("%lu\n", (unsigned long)bench_results.dhrystones);
        else
            printf("%s\n", get_string(MSG_NA));

        printf("MIPS: ");
        if (bench_results.benchmarks_valid) {
            format_scaled(buffer, sizeof(buffer), bench_results.mips, TRUE);
            printf("%s\n", buffer);
        } else {
            printf("%s\n", get_string(MSG_NA));
        }

        printf("MFLOPS: ");
        if (hw_info.fpu_type != FPU_NONE && bench_results.benchmarks_valid
            && hw_info.fpu_enabled) {
            format_scaled(buffer, sizeof(buffer), bench_results.mflops, TRUE);
            printf("%s\n", buffer);
        } else {
            printf("%s\n", get_string(MSG_NA));
        }

        if (bench_results.benchmarks_valid && bench_results.chip_speed > 0)
            format_scaled(buffer, sizeof(buffer),
                          bench_results.chip_speed / 10000, TRUE);
        else
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
        printf("Chip RAM speed: %s MB/s\n", buffer);

        if (bench_results.benchmarks_valid && bench_results.fast_speed > 0)
            format_scaled(buffer, sizeof(buffer),
                          bench_results.fast_speed / 10000, TRUE);
        else
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
        printf("Fast RAM speed: %s MB/s\n", buffer);

        if (bench_results.benchmarks_valid && bench_results.rom_speed > 0)
            format_scaled(buffer, sizeof(buffer),
                          bench_results.rom_speed / 10000, TRUE);
        else
            snprintf(buffer, sizeof(buffer), "%s", get_string(MSG_NA));
        printf("ROM speed: %s MB/s\n", buffer);
    }

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
    debug(XSYSINFO_NAME " open_libraries: trying intuition.library\n");
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", MIN_INTUITION_VERSION);
    if (!IntuitionBase) {
        Printf((CONST_STRPTR)"Could not open intuition.library v%d\n",
               MIN_INTUITION_VERSION);
        return FALSE;
    }

    /* Open graphics.library */
    debug(XSYSINFO_NAME " open_libraries: trying graphics.library\n");
    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", MIN_GRAPHICS_VERSION);
    if (!GfxBase) {
        Printf((CONST_STRPTR)"Could not open graphics.library v%d\n",
               MIN_GRAPHICS_VERSION);
        return FALSE;
    }

    /* Open identify.library */
    debug(XSYSINFO_NAME " open_libraries: trying identify.library\n");
    IdentifyBase = OpenLibrary((CONST_STRPTR) "identify.library", MIN_IDENTIFY_VERSION);
    /*
    if (!IdentifyBase) {
        Printf((CONST_STRPTR)"%s\n", (LONG)get_string(MSG_ERR_NO_IDENTIFY));
    }
    */

    app->IdentifyBase = IdentifyBase;

    debug(XSYSINFO_NAME " open_libraries: trying icon.library\n");

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
 * Open display - either a window on Workbench or a custom screen
 */
static BOOL open_display(void)
{
    struct NewScreen *newScreen;
    struct NewWindow *newWindow;
    BOOL use_window = FALSE;

    /* Check display mode setting from tooltypes */
    if (app->display_mode == DISPLAY_WINDOW) {
        use_window = TRUE;
    } else if (app->display_mode == DISPLAY_SCREEN) {
        use_window = FALSE;
    } else {
        /* AUTO mode - detect based on RTG */
        if (wb_startup) {
            /* Get a copy of Workbench screen to check its mode */
            struct Screen *wb_screen = (struct Screen *)AllocMem(sizeof(struct Screen), MEMF_ANY | MEMF_CLEAR);
            if (wb_screen) {
                if (GetScreenData(wb_screen, sizeof(struct Screen), WBENCHSCREEN, NULL)) {
                    /* If Workbench is in RTG mode: resolution exceeds native chipset limits */
                    if (wb_screen->Width > RTG_WIDTH_THRESHOLD ||
                        wb_screen->Height > RTG_HEIGHT_THRESHOLD) {
                        use_window = TRUE;
                    }
                }
                FreeMem(wb_screen, sizeof(struct Screen));
            }
        }
    }

    /* Determine if system is PAL or NTSC */
    app->is_pal = (GfxBase->DisplayFlags & PAL) ? TRUE : FALSE;

    if (use_window) {
        debug(XSYSINFO_NAME " open_display: opening window\n");

        /* Open window on Workbench */
        app->use_custom_screen = FALSE;

        newWindow = (struct NewWindow *)AllocMem(sizeof(struct NewWindow), MEMF_ANY | MEMF_CLEAR);
        if (newWindow) {
            newWindow->Title = (UBYTE *)(XSYSINFO_NAME " " XSYSINFO_VERSION);
            newWindow->Type = WBENCHSCREEN;
            newWindow->Width = SCREEN_WIDTH;
            newWindow->Height = SCREEN_HEIGHT_NTSC + 16;
            newWindow->IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS |
                    IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                    IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
            newWindow->Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                    WFLG_ACTIVATE | WFLG_SMART_REFRESH | WFLG_GIMMEZEROZERO |
                    WFLG_REPORTMOUSE;
            if (hw_info.kickstart_patch_version < 36)
                newWindow->BlockPen = 1;

            app->window = OpenWindow(newWindow);
            FreeMem(newWindow, sizeof(struct NewWindow));
        }

        if (!app->window) {
            return FALSE;
        }

        app->rp = app->window->RPort;
        app->screen = app->window->WScreen;
        app->screen_height = app->screen->Height;

        /* If default screen font is larger than Topaz8, switch to Topaz8 */
        if (app->window->IFont->tf_YSize > Topaz8Font.ta_YSize)
        {
            app->tf = OpenFont(&Topaz8Font);
            if (app->tf)
            {
                SetFont(app->rp, app->tf);
            }
        }
    } else {
        debug(XSYSINFO_NAME " open_display: opening screen\n");

        /* Open custom screen */
        app->use_custom_screen = TRUE;

        app->screen_height = app->is_pal ? SCREEN_HEIGHT_PAL : SCREEN_HEIGHT_NTSC;

        newScreen = (struct NewScreen *)AllocMem(sizeof(struct NewScreen), MEMF_ANY | MEMF_CLEAR);
        if (newScreen) {
            newScreen->Width = SCREEN_WIDTH;
            newScreen->Height = app->screen_height;
            newScreen->Depth = SCREEN_DEPTH;
            newScreen->DefaultTitle = (UBYTE *)(XSYSINFO_NAME " " XSYSINFO_VERSION);
            newScreen->Type = CUSTOMSCREEN;
            newScreen->Font = &Topaz8Font;
            newScreen->ViewModes = HIRES;
            app->screen = OpenScreen(newScreen);
            ShowTitle(app->screen, FALSE);
            FreeMem(newScreen, sizeof(struct NewScreen));
        }

        if (!app->screen) {
            Printf((CONST_STRPTR)"%s\n", get_string(MSG_ERR_NO_SCREEN));
            return FALSE;
        }

        /* Set our palette */
        set_palette();

        /* Open borderless window on our screen */
        newWindow = (struct NewWindow *)AllocMem(sizeof(struct NewWindow), MEMF_ANY | MEMF_CLEAR);
        if (newWindow) {
            newWindow->Type = CUSTOMSCREEN;
            newWindow->Width = SCREEN_WIDTH;
            newWindow->Height = app->screen_height;
            newWindow->IDCMPFlags = IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW |
                        IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
            newWindow->Flags = WFLG_BORDERLESS | WFLG_ACTIVATE |
                        WFLG_RMBTRAP | WFLG_SMART_REFRESH | WFLG_REPORTMOUSE;
            newWindow->Screen = app->screen;
            app->window = OpenWindow(newWindow);
            FreeMem(newWindow, sizeof(struct NewWindow));
        }

        if (!app->window) {
            CloseScreen(app->screen);
            app->screen = NULL;
            return FALSE;
        }

        app->rp = app->window->RPort;
    }
    debug(XSYSINFO_NAME " open_display: allocating pens\n");

    /* Allocate/map pens for drawing */
    allocate_pens();
    debug(XSYSINFO_NAME " open_display: finished\n");

    return TRUE;
}

/*
 * Strip and reply all pending IntuiMessages for a window on a port.
 *
 * We must not rely on ln_Succ after replying a message, so we
 * capture the successor before each ReplyMsg.
 */
static void StripIntuiMessages(struct MsgPort *mp, struct Window *win)
{
    struct IntuiMessage *msg;
    struct Node *succ;

    msg = (struct IntuiMessage *)mp->mp_MsgList.lh_Head;
    while ((succ = msg->ExecMessage.mn_Node.ln_Succ)) {
        if (msg->IDCMPWindow == win) {
            Remove((struct Node *)msg);
            ReplyMsg((struct Message *)msg);
        }
        msg = (struct IntuiMessage *)succ;
    }
}

/*
 * Safely close a window by first draining any unreplied IntuiMessages.
 *
 * Without this, closing a window that still has pending messages on its
 * UserPort causes Intuition to access freed memory.
 */
static void CloseWindowSafely(struct Window *win)
{
    Forbid();
    StripIntuiMessages(win->UserPort, win);
    win->UserPort = NULL;
    ModifyIDCMP(win, 0L);
    Permit();
    CloseWindow(win);
}

/*
 * Close display
 */
static void close_display(void)
{
    /* Release any allocated pens before closing */
    release_pens();

    if (app->tf) {
        CloseFont(app->tf);
        app->tf = NULL;
    }

    if (app->window) {
        CloseWindowSafely(app->window);
        app->window = NULL;
        if (!app->use_custom_screen) {
            app->screen = NULL;
            app->screen_height = 0;
        }
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

    if (app->use_custom_screen || hw_info.kickstart_patch_version < 36) {
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

    if (!app->pens_allocated || !app->screen || SysBase->LibNode.lib_Version <= 34) {
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
		    (address >= 0xE00000 && address < 0xE80000)) {
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
