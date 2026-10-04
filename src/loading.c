// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

/* One executable serves as the START/STOP command and its own background
 * display process. CreateProc enters a separately loaded copy without a CLI. */
#include "loading.h"
#include <string.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/copper.h>
#include <graphics/gfxbase.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

/* DOS passes command arguments in A0/D0. The background process has no
 * command line, so loading_entry checks pr_CLI before touching those values.
 * File-scope assembly supplies both wrappers without a C function prologue. */
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    move.l d0,-(sp)\n"
    "    move.l a0,-(sp)\n"
    "    jsr _loading_entry\n"
    "    addq.l #8,sp\n"
    "    rts\n"
    ".globl _plasma_vblank_server\n"
    "_plasma_vblank_server:\n"
    "    movem.l d1-d7/a0-a6,-(sp)\n"
    "    jsr _plasma_vblank_tick\n"
    "    movem.l (sp)+,d1-d7/a0-a6\n"
    "    moveq #0,d0\n"
    "    rts\n"
    ".balign 4,0\n");

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

struct LoadingControl {
    struct MsgPort port;
    BPTR segment;
    char port_name[sizeof(LOADING_CONTROL_PORT)];
    char task_name[sizeof(LOADING_TASK_NAME)];
};

static void stop_scroller(void)
{
    struct LoadingControl *control;
    struct Task *task;
    int ticks;

    Forbid();
    control = (struct LoadingControl *)FindPort((CONST_STRPTR)LOADING_CONTROL_PORT);
    task = FindTask((CONST_STRPTR)LOADING_TASK_NAME);
    if (control && task)
        Signal(task, SIGBREAKF_CTRL_C);
    Permit();
    if (!control)
        return;

    for (ticks = 0; ticks < 100; ++ticks) {
        Forbid();
        task = FindTask((CONST_STRPTR)LOADING_TASK_NAME);
        Permit();
        if (!task)
            break;
        Delay(1);
    }
    if (task)
        return; /* Its code is still in use; keep the segment loaded. */

    RemPort(&control->port);
    UnLoadSeg(control->segment);
    FreeMem(control, sizeof(*control));
}

static void start_scroller(void)
{
    struct LoadingControl *control;
    BPTR segment;

    Forbid();
    control = (struct LoadingControl *)FindPort((CONST_STRPTR)LOADING_CONTROL_PORT);
    Permit();
    if (control)
        return;

    segment = LoadSeg((CONST_STRPTR)"C:xSysInfoLoader");
    if (!segment)
        return; /* The GUI must still boot without the splash. */

    control = AllocMem(sizeof(*control), MEMF_PUBLIC | MEMF_CLEAR);
    if (!control) {
        UnLoadSeg(segment);
        return;
    }
    control->segment = segment;
    memcpy(control->port_name, LOADING_CONTROL_PORT, sizeof(control->port_name));
    memcpy(control->task_name, LOADING_TASK_NAME, sizeof(control->task_name));
    control->port.mp_Node.ln_Name = control->port_name;
    control->port.mp_Node.ln_Type = NT_MSGPORT;
    control->port.mp_Flags = PA_IGNORE;
    NewList(&control->port.mp_MsgList);
    AddPort(&control->port);

    /* Wake at each VBlank ahead of the loading CLI, then sleep again.
     * Equal priority makes animation depend on DOS's task time slices. */
    if (!CreateProc((STRPTR)control->task_name, 1, segment, 4096)) {
        RemPort(&control->port);
        FreeMem(control, sizeof(*control));
        UnLoadSeg(segment);
    }
}

static void loading_scroller_entry(void);

LONG loading_entry(const char *arguments, LONG length)
{
    struct Process *process;

    __asm__ volatile ("move.l 4.w,%0" : "=r" (SysBase));
    process = (struct Process *)FindTask(NULL);
    if (!process->pr_CLI) {
        loading_scroller_entry();
        return RETURN_OK;
    }

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (!DOSBase)
        return RETURN_OK;

    while (length > 0 && (*arguments == ' ' || *arguments == '\t')) {
        ++arguments;
        --length;
    }
    if (length >= 4 && arguments[0] == 'S' && arguments[1] == 'T' &&
        arguments[2] == 'O' && arguments[3] == 'P' &&
        (length == 4 || arguments[4] == ' ' || arguments[4] == '\t' ||
         arguments[4] == '\r' || arguments[4] == '\n'))
        stop_scroller();
    else
        start_scroller();

    CloseLibrary((struct Library *)DOSBase);
    return RETURN_OK;
}

#define PLASMA_COLUMNS 49
#define PLASMA_ROWS 200
#define PLASMA_ROW_WORDS ((PLASMA_COLUMNS + 1) * 2)
#define RAMP_WORDS 384
#define COPPER_BLOCK_INSTRUCTIONS 32
#define SCROLL_WIDTH ((sizeof(message) - 1) * 8)
#define LARGE_GLYPH_ROWS 9

static const char logo[] = "xSysInfo";
static const char version[] = "v" XSYSINFO_VERSION;
static const char message[] = "  Loading xSysInfo v" XSYSINFO_VERSION
    " - presented to you by Stefan Reinauer    ";
static struct TextAttr topaz = {
    (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT
};
static struct UCopList *plasma_list;
static UBYTE glyph_mask[LARGE_GLYPH_ROWS * 320];
static UWORD sine_vertical[120];
static BYTE sine_horizontal[90];
static UWORD *color_ramp;
static volatile UWORD vertical_phase;
static unsigned int horizontal_phase;
static struct cprlist *mapped_copper;
static UWORD *mapped_start;
static UWORD *mapped_anchor;
static UWORD *mapped_color_data;
static struct Interrupt vblank_server;
static volatile UWORD vblank_ticks;
static UWORD scroll_column;
static UBYTE scroll_pixels[8][SCROLL_WIDTH];
static UBYTE *scroll_plane;
static UWORD scroll_row_stride;

static const UWORD vertical_quarter[30] = {
    202, 212, 224, 234, 244, 254, 264, 274, 284, 292,
    302, 310, 320, 328, 336, 342, 350, 356, 362, 368,
    374, 378, 384, 386, 390, 394, 396, 398, 398, 398
};
static const BYTE horizontal_quarter[23] = {
     0,  0,  2,  4,  6,  6,  8, 10, 10, 12, 14, 14,
    16, 16, 18, 18, 18, 20, 20, 20, 20, 20, 20
};

static void init_plasma_waves(void)
{
    static const UWORD plasma_palette[16] = {
        0x000, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,
        0xf00, 0xf20, 0xf40, 0xf60, 0xf80, 0xfa0, 0xfc0, 0xff0
    };
    unsigned int i, level = 0, repeat = 0;

    for (i = 0; i < 30; ++i) {
        sine_vertical[i] = vertical_quarter[i];
        sine_vertical[59 - i] = vertical_quarter[i];
        sine_vertical[60 + i] = 398 - vertical_quarter[i];
        sine_vertical[119 - i] = 398 - vertical_quarter[i];
    }
    for (i = 0; i < 23; ++i) {
        sine_horizontal[i] = horizontal_quarter[i];
        sine_horizontal[44 - i] = horizontal_quarter[i];
        sine_horizontal[45 + i] = -horizontal_quarter[i];
        sine_horizontal[89 - i] = -horizontal_quarter[i];
    }
    for (i = 106; i < 152; ++i) {
        color_ramp[i] = plasma_palette[level];
        if (++repeat == 3) {
            repeat = 0;
            ++level;
        }
    }
    level = 15;
    repeat = 0;
    for (i = 152; i < 200; ++i) {
        color_ramp[i] = plasma_palette[level];
        if (++repeat == 3) {
            repeat = 0;
            --level;
        }
    }
}

static unsigned int vertical_offset(unsigned int phase, unsigned int column)
{
    unsigned int x = phase + 2 * column;

    if (x >= 120)
        x -= 120;
    /* Scale the bootblock's 264-line plasma into our 200-line screen. */
    return ((sine_vertical[x] >> 1) * 3) >> 2;
}

static UWORD plasma_color(unsigned int row, unsigned int column,
                          unsigned int phase)
{
    return color_ramp[row + vertical_offset(phase, column)];
}

static struct UCopList *make_plasma_list(void)
{
    struct UCopList *list;
    unsigned int row, column, wave_row = 0;

    list = AllocMem(sizeof(*list), MEMF_PUBLIC | MEMF_CLEAR);
    if (!list)
        return NULL;
    /* CBump allocates another block when this one fills. Small blocks also
     * work when boot-time memory is too fragmented for one huge buffer. */
    CINIT(list, COPPER_BLOCK_INSTRUCTIONS);
    if (!list->FirstCopList) {
        FreeMem(list, sizeof(*list));
        return NULL;
    }

    for (row = 0; row < PLASMA_ROWS; ++row) {
        CWAIT(list, row, 0x2f + sine_horizontal[wave_row]);
        for (column = 0; column < PLASMA_COLUMNS; ++column) {
            CMove(list, (APTR)0xdff180,
                  plasma_color(row, column, 0));
            CBump(list);
        }
        if (++wave_row == 90)
            wave_row = 0;
    }
    CEND(list);
    return list;
}

/* MrgCop has copied the user instructions to Chip RAM. Locate the COLOR00
 * words in that hardware list once, then update them in place as the
 * bootblock does. Re-merging the display each frame stalls floppy I/O. */
/* The merged instructions are contiguous only if no system Copper moves
 * interrupt the 49 colors on a line. Check that layout before using the
 * original demo's one-word-wide, 200-line blitter copies. */
static UWORD *find_blitter_target(struct cprlist *list)
{
    struct cprlist *block;
    unsigned int blocks = 0;

    for (block = list; block && blocks++ < 16; block = block->Next) {
        unsigned int i, row, column;
        unsigned int instructions = (unsigned int)block->MaxCount;
        if (!block->start || block->MaxCount < PLASMA_ROWS *
            (PLASMA_COLUMNS + 1) || block->MaxCount > 16000)
            continue;
        for (i = 1; i + PLASMA_ROWS * (PLASMA_COLUMNS + 1) <=
                    instructions; ++i) {
            UWORD *move = &block->start[i * 2];
            if (move[0] != 0x0180 || !(move[-2] & 1))
                continue;
            for (row = 0; row < PLASMA_ROWS; ++row) {
                UWORD *line = move + row * PLASMA_ROW_WORDS;
                if (!(line[-2] & 1))
                    break;
                for (column = 0; column < PLASMA_COLUMNS; ++column)
                    if (line[column * 2] != 0x0180)
                        break;
                if (column != PLASMA_COLUMNS)
                    break;
            }
            if (row == PLASMA_ROWS)
                return move + 1;
        }
    }
    return NULL;
}

/* CopLStart can point to a different part of the merged list when Intuition
 * adds another screen. Use it as a quick hint, then search the full list. */
static UWORD *find_plasma_target(struct cprlist *active)
{
    struct cprlist *block;
    UWORD *anchor;
    unsigned int blocks = 0;

    if (!active || !plasma_list || !plasma_list->FirstCopList)
        return NULL;
    anchor = plasma_list->FirstCopList->CopLStart;
    for (block = active; anchor && block && blocks++ < 16;
         block = block->Next) {
        UWORD *end;
        unsigned int i, row, column;
        if (!block->start || block->MaxCount < 10000 ||
            block->MaxCount > 16000)
            continue;
        end = block->start + (unsigned int)block->MaxCount * 2;
        if ((ULONG)anchor < (ULONG)block->start ||
            (ULONG)anchor >= (ULONG)end)
            continue;
        for (i = 0; i < 64; ++i) {
            UWORD *move = anchor + i * 2;
            if ((ULONG)move < (ULONG)(block->start + 2) ||
                (ULONG)(move + PLASMA_ROWS * PLASMA_ROW_WORDS) >
                    (ULONG)end)
                break;
            if (move[0] != 0x0180 || !(move[-2] & 1))
                continue;
            for (row = 0; row < PLASMA_ROWS; ++row) {
                UWORD *line = move + row * PLASMA_ROW_WORDS;
                if (!(line[-2] & 1))
                    break;
                for (column = 0; column < PLASMA_COLUMNS; ++column)
                    if (line[column * 2] != 0x0180)
                        break;
                if (column != PLASMA_COLUMNS)
                    break;
            }
            if (row == PLASMA_ROWS)
                return move + 1;
        }
    }
    return find_blitter_target(active);
}

static void scroll_vblank_tick(void)
{
    unsigned int row;

    for (row = 175; row <= 182; ++row) {
        UBYTE *line = scroll_plane + row * scroll_row_stride;
        UBYTE next = scroll_pixels[row - 175][scroll_column];
        unsigned int byte;
        for (byte = 0; byte < 39; ++byte) {
            line[byte] = (line[byte] << 1) | (line[byte + 1] >> 7);
        }
        line[39] = (line[39] << 1) | next;
    }
    if (++scroll_column == SCROLL_WIDTH)
        scroll_column = 0;
}

/* Called by the Exec VBlank server. This path uses no OS calls, so it keeps
 * the source's horizontal sine and the text moving while a DOS operation
 * prevents the loader process from being scheduled. */
void plasma_vblank_tick(void)
{
    struct cprlist *active;
    UWORD *anchor;
    UWORD *target;
    unsigned int row, wave_row;

    ++vblank_ticks;
    if (!scroll_plane)
        return;
    scroll_vblank_tick();

    active = GfxBase->ActiView ? GfxBase->ActiView->LOFCprList : NULL;
    anchor = plasma_list && plasma_list->FirstCopList ?
             plasma_list->FirstCopList->CopLStart : NULL;
    /* The task locates and validates the Copper layout. Never scan it in
     * VBlank: a failed search used to stall every eighth scroll pixel.
     * Skip plasma writes until the task has mapped a changed display. */
    target = active && active == mapped_copper &&
             active->start == mapped_start && anchor == mapped_anchor ?
             mapped_color_data : NULL;
    if (target) {
        wave_row = horizontal_phase;
        for (row = 0; row < PLASMA_ROWS; ++row) {
            UWORD *wait = target - 3 + row * PLASMA_ROW_WORDS;
            *wait = (*wait & 0xff00) |
                    (0x2f + sine_horizontal[wave_row]);
            if (++wave_row == 90)
                wave_row = 0;
        }
    }

    /* Half-speed waves share the VBlank clock, independent of DOS scheduling.
     * Keep the horizontal wave updating every frame; advance the vertical
     * wave every other frame. The task snapshots it for all 49 blits. */
    if (!(vblank_ticks & 1) && ++vertical_phase == 120)
        vertical_phase = 0;
    horizontal_phase += 3;
    if (horizontal_phase >= 90)
        horizontal_phase -= 90;
}

static void update_plasma(void)
{
    volatile struct Custom *hw = (volatile struct Custom *)0xdff000;
    struct cprlist *active;
    UWORD *anchor;
    UWORD *target;
    unsigned int column, phase;

    /* Acquire before forbidding: OwnBlitter may wait for another owner.
     * Keep the display list alive until the last DMA write has finished. */
    OwnBlitter();
    WaitBlit();
    Forbid();
    active = GfxBase->ActiView ? GfxBase->ActiView->LOFCprList : NULL;
    anchor = plasma_list && plasma_list->FirstCopList ?
             plasma_list->FirstCopList->CopLStart : NULL;
    if (active != mapped_copper ||
        (active && active->start != mapped_start) || anchor != mapped_anchor) {
        target = find_plasma_target(active);
        /* Publish one consistent mapping to VBlank, including NULL when
         * the layout is unsuitable. Retry only when the display changes. */
        Disable();
        mapped_copper = active;
        mapped_start = active ? active->start : NULL;
        mapped_anchor = anchor;
        mapped_color_data = target;
        Enable();
    }
    target = active ? mapped_color_data : NULL;
    phase = vertical_phase;

    if (target) {
        hw->bltcon0 = 0x09f0;
        hw->bltcon1 = 0;
        hw->bltafwm = 0xffff;
        hw->bltalwm = 0xffff;
        hw->bltamod = 0;
        hw->bltdmod = (PLASMA_ROW_WORDS - 1) * 2;
        for (column = 0; column < PLASMA_COLUMNS; ++column) {
            WaitBlit();
            hw->bltapt = (APTR)&color_ramp[vertical_offset(phase,
                                                          column)];
            hw->bltdpt = (APTR)(target + column * 2);
            hw->bltsize = (PLASMA_ROWS << 6) | 1;
        }
        WaitBlit();
    }
    DisownBlitter();
    Permit();
}

static void free_plasma_list(void)
{
    if (plasma_list) {
        struct ViewPort detached = { 0 };
        detached.UCopIns = plasma_list;
        FreeVPortCopLists(&detached);
        plasma_list = NULL;
    }
}

/* This loader has no C startup. Keep small geometry products on the 68000
 * MULU instruction instead of calling libgcc's utility.library helper. */
static unsigned int mul_u16(unsigned int a, unsigned int b)
{
    UWORD right = (UWORD)b;
    __asm__ volatile ("mulu.w %1,%0" : "+d" (a) : "d" (right) : "cc");
    return a;
}

/* Enlarge the ROM font with a one-pixel bevel. Dropping only the trailing
 * blank columns lets the longer build version fit without shrinking height. */
static void draw_beveled_text(struct RastPort *rp, struct RastPort *scratch,
                             const char *text,
                             unsigned int length, WORD top,
                             unsigned int scale, unsigned int columns,
                             UBYTE dark_pen, UBYTE face_pen, UBYTE light_pen)
{
    unsigned int x, y;
    unsigned int source_width = length << 3;
    unsigned int drawn_width = mul_u16(mul_u16(length, columns), scale);
    WORD left;

    if (!length || source_width > 320 || drawn_width > 320)
        return;
    left = (320 - drawn_width) / 2;

    SetRast(scratch, 0);
    SetAPen(scratch, 1);
    Move(scratch, 0, 7);
    Text(scratch, (STRPTR)text, length);
    for (y = 0; y < LARGE_GLYPH_ROWS; ++y)
        for (x = 0; x < source_width; ++x)
            glyph_mask[mul_u16(y, source_width) + x] =
                ReadPixel(scratch, x, y) != 0;

    SetAPen(rp, face_pen);
    for (y = 0; y < LARGE_GLYPH_ROWS; ++y) {
        for (x = 0; x < source_width; ++x) {
            WORD px, py;
            unsigned int cell = x & 7;
            if (cell >= columns ||
                !glyph_mask[mul_u16(y, source_width) + x])
                continue;
            px = left + mul_u16(mul_u16(x >> 3, columns) + cell, scale);
            py = top + mul_u16(y, scale);
            RectFill(rp, px, py, px + scale - 1, py + scale - 1);
        }
    }
    for (y = 0; y < LARGE_GLYPH_ROWS; ++y) {
        for (x = 0; x < source_width; ++x) {
            WORD px, py;
            unsigned int cell = x & 7;
            if (cell >= columns ||
                !glyph_mask[mul_u16(y, source_width) + x])
                continue;
            px = left + mul_u16(mul_u16(x >> 3, columns) + cell, scale);
            py = top + mul_u16(y, scale);
            SetAPen(rp, light_pen);
            if (y == 0 ||
                !glyph_mask[mul_u16(y - 1, source_width) + x])
                RectFill(rp, px, py, px + scale - 1, py);
            if (cell == 0 ||
                !glyph_mask[mul_u16(y, source_width) + x - 1])
                RectFill(rp, px, py, px, py + scale - 1);
            SetAPen(rp, dark_pen);
            if (y + 1 == LARGE_GLYPH_ROWS ||
                !glyph_mask[mul_u16(y + 1, source_width) + x])
                RectFill(rp, px, py + scale - 1,
                         px + scale - 1, py + scale - 1);
            if (cell + 1 == columns ||
                !glyph_mask[mul_u16(y, source_width) + x + 1])
                RectFill(rp, px + scale - 1, py,
                         px + scale - 1, py + scale - 1);
        }
    }
}

static void prepare_scroller(struct RastPort *rp, struct RastPort *scratch)
{
    struct BitMap *bitmap = rp->BitMap;
    unsigned int x, y, offset;

    /* Rasterize in screen-width chunks so long credits and build versions
     * reach the scrolling buffer without being clipped by the RastPort. */
    for (offset = 0; offset < sizeof(message) - 1; offset += 40) {
        unsigned int length = sizeof(message) - 1 - offset;
        if (length > 40)
            length = 40;
        SetRast(scratch, 0);
        SetAPen(scratch, 1);
        Move(scratch, 0, scratch->TxBaseline);
        Text(scratch, (STRPTR)message + offset, length);
        for (y = 0; y < 8; ++y)
            for (x = 0; x < length * 8; ++x)
                scroll_pixels[y][offset * 8 + x] =
                    ReadPixel(scratch, x, y) != 0;
    }

    scroll_plane = bitmap->Planes[2];
    scroll_row_stride = bitmap->BytesPerRow *
        ((bitmap->Flags & BMF_INTERLEAVED) ? bitmap->Depth : 1);
}

static void seed_scroller(void)
{
    unsigned int row, x, column;

    for (row = 0; row < 8; ++row) {
        UBYTE *line = scroll_plane + (175 + row) * scroll_row_stride;
        column = 0;
        for (x = 0; x < 320; ++x) {
            if (scroll_pixels[row][column]) {
                UBYTE bit = 0x80 >> (x & 7);
                line[x >> 3] |= bit;
            }
            if (++column == SCROLL_WIDTH)
                column = 0;
        }
    }
    scroll_column = column;
}

extern void plasma_vblank_server(void);

static void loading_scroller_entry(void)
{
    struct NewScreen spec = { 0 };
    struct NewWindow window_spec = { 0 };
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    UWORD *blank_pointer = NULL;
    struct BitMap scratch_bitmap = { 0 };
    struct RastPort scratch;
    struct RastPort *rp;
    unsigned int version_core_length = 0;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    if (!GfxBase)
        return;
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", 0);
    if (!IntuitionBase)
        goto close_graphics;

    spec.Width = 320;
    spec.Height = 200;
    spec.Depth = 3;
    spec.DetailPen = 1;
    spec.BlockPen = 0;
    spec.Type = CUSTOMSCREEN | SCREENBEHIND;
    spec.Font = &topaz;
    screen = OpenScreen(&spec);
    if (!screen)
        goto close_intuition;
    ShowTitle(screen, FALSE);

    /* A window-local transparent sprite restores the normal pointer when
     * xSysInfo takes over, without changing global sprite colors or DMA. */
    blank_pointer = AllocMem(6 * sizeof(*blank_pointer), MEMF_CHIP | MEMF_CLEAR);
    if (!blank_pointer)
        goto close_screen;
    window_spec.Width = spec.Width;
    window_spec.Height = spec.Height;
    window_spec.Type = CUSTOMSCREEN;
    window_spec.Screen = screen;
    window_spec.Flags = WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_RMBTRAP;
    window = OpenWindow(&window_spec);
    if (!window)
        goto close_screen;
    SetPointer(window, blank_pointer, 1, 16, 0, 0);

    SetRGB4(&screen->ViewPort, 0, 0, 0, 1);
    SetRGB4(&screen->ViewPort, 1, 1, 0, 3);
    SetRGB4(&screen->ViewPort, 2, 5, 2, 8);
    SetRGB4(&screen->ViewPort, 3, 8, 5, 11);
    SetRGB4(&screen->ViewPort, 4, 10, 10, 10);
    SetRGB4(&screen->ViewPort, 5, 0, 1, 2);
    SetRGB4(&screen->ViewPort, 6, 0, 2, 4);
    SetRGB4(&screen->ViewPort, 7, 0, 3, 6);
    rp = &screen->RastPort;
    SetRast(rp, 0);
    /* The first screen may become visible even with SCREENBEHIND. Keep all
     * temporary glyphs in a separate bitmap that never reaches the display. */
    InitBitMap(&scratch_bitmap, 1, 320, 16);
    scratch_bitmap.Planes[0] = AllocRaster(320, 16);
    if (!scratch_bitmap.Planes[0])
        goto close_screen;
    InitRastPort(&scratch);
    scratch.BitMap = &scratch_bitmap;
    SetFont(&scratch, rp->Font);
    color_ramp = AllocMem(RAMP_WORDS * sizeof(*color_ramp),
                          MEMF_CHIP | MEMF_CLEAR);
    if (!color_ramp)
        goto close_screen;
    init_plasma_waves();
    draw_beveled_text(rp, &scratch, logo, sizeof(logo) - 1, 28, 5, 7,
                      1, 2, 3);
    while (version[version_core_length] &&
           version[version_core_length] != '-')
        ++version_core_length;
    draw_beveled_text(rp, &scratch, version, version_core_length, 100, 3, 7,
                      5, 6, 7);
    if (version[version_core_length])
        draw_beveled_text(rp, &scratch, version + version_core_length + 1,
                          sizeof(version) - 2 - version_core_length,
                          132, 2, 6, 5, 6, 7);
    prepare_scroller(rp, &scratch);
    WaitBlit();
    FreeRaster(scratch_bitmap.Planes[0], 320, 16);
    scratch_bitmap.Planes[0] = NULL;
    seed_scroller();
    plasma_list = make_plasma_list();
    if (plasma_list) {
        Forbid();
        screen->ViewPort.UCopIns = plasma_list;
        Permit();
        MakeScreen(screen);
        RethinkDisplay();
    }

    WaitBlit();
    ActivateWindow(window);
    ScreenToFront(screen);

    vblank_server.is_Node.ln_Type = NT_INTERRUPT;
    vblank_server.is_Node.ln_Name = (char *)"xSysInfo plasma";
    vblank_server.is_Code = plasma_vblank_server;
    AddIntServer(INTB_VERTB, &vblank_server);

    while (!(SetSignal(0, 0) & SIGBREAKF_CTRL_C)) {
        WaitTOF();
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        if (plasma_list)
            update_plasma();
    }

    RemIntServer(INTB_VERTB, &vblank_server);
    scroll_plane = NULL;
    CloseWindow(window);
    window = NULL;
    if (plasma_list) {
        Forbid();
        screen->ViewPort.UCopIns = NULL;
        Permit();
        /* CloseScreen removes this viewport and rebuilds the display once.
         * Rebuilding it separately exposes Workbench before xSysInfo. */
        CloseScreen(screen);
        screen = NULL;
        if (IntuitionBase->FirstScreen)
            ScreenToFront(IntuitionBase->FirstScreen);
        free_plasma_list();
    }
    FreeMem(color_ramp, RAMP_WORDS * sizeof(*color_ramp));
close_screen:
    if (scratch_bitmap.Planes[0]) {
        WaitBlit();
        FreeRaster(scratch_bitmap.Planes[0], 320, 16);
    }
    if (window)
        CloseWindow(window);
    if (screen)
        CloseScreen(screen);
    if (blank_pointer)
        FreeMem(blank_pointer, 6 * sizeof(*blank_pointer));
close_intuition:
    CloseLibrary((struct Library *)IntuitionBase);
close_graphics:
    CloseLibrary((struct Library *)GfxBase);
}
