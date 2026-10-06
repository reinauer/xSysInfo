// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/*
 * xSysInfo - MUI interface
 *
 * Presents the same information as the classic interface on register
 * pages in a resizable MUI window. It is selected with the MUI switch or
 * the DISPLAY=MUI tooltype and only needs MUI 3.8
 * at runtime; without it, main.c falls back to the classic interface.
 */

#include <string.h>
#include <stdio.h>

#include <exec/execbase.h>
#include <intuition/classusr.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <utility/hooks.h>

#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "xsysinfo.h"
#include "benchmark.h"
#include "boards.h"
#include "cache.h"
#include "clock.h"
#include "debug.h"
#include "drives.h"
#include "format.h"
#include "display.h"
#include "hardware.h"
#include "locale_str.h"
#include "memory.h"
#include "mui_gui.h"
#include "print.h"
#include "scsi.h"
#include "software.h"

/*
 * MUI SDKs disagree on char vs. unsigned char strings, e.g. the object
 * macros pass MUIC_* class names where newer SDKs expect CONST_STRPTR.
 * The pointers are compatible, so only the signedness warning is dropped.
 */
#pragma GCC diagnostic ignored "-Wpointer-sign"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) \
    ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

/* Supported by MUI 3.8, but omitted from its original public headers. */
#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys 0x80424c36
#endif

#define MUI_MIN_VERSION 15      /* MUIA_Window_DisableKeys */
#define NUM_SPEED_ROWS  (NUM_REFERENCE_SYSTEMS + 1)   /* "You" first */
#define INFO_COLUMN_SPACING 12

struct Library *MUIMasterBase = NULL;

/* Values returned by MUIM_Application_NewInput */
enum {
    ID_ABOUT = 1,
    ID_ABOUT_MUI,
    ID_MUI_PREFS,
    ID_BENCHMARK,
    ID_SAVE,
    ID_SOFTWARE_PAGE,
    ID_SCALE,
    ID_MEMORY_SELECT,
    ID_MEMORY_SPEED,
    ID_DRIVE_SELECT,
    ID_DRIVE_SPEED,
    ID_DRIVE_SCSI,
    ID_SCSI_CLOSE,
    ID_BOARD_DISPLAY,
    ID_BOARD_SELECT,
    ID_BOARD_DETAILS,
    ID_BOARD_DETAILS_CLOSE,
    ID_CACHE_BASE           /* ID_CACHE_BASE + CacheSetting; must be last */
};

/* Register pages in display order */
enum {
    PAGE_SOFTWARE, PAGE_HARDWARE, PAGE_SPEED, PAGE_MEMORY, PAGE_DRIVES, PAGE_BOARDS, PAGE_COUNT
};

enum {
    MEM_START, MEM_END, MEM_TOTAL, MEM_TYPE, MEM_PRIORITY, MEM_LOWER,
    MEM_UPPER, MEM_FIRST, MEM_FREE, MEM_LARGEST, MEM_CHUNKS, MEM_NODE,
    MEM_SPEED, MEM_COUNT
};

static const LocaleStringID memory_rows[MEM_COUNT] = {
    MSG_START_ADDRESS, MSG_END_ADDRESS, MSG_TOTAL_SIZE,
    MSG_MEMORY_TYPE, MSG_PRIORITY, MSG_LOWER_BOUND,
    MSG_UPPER_BOUND, MSG_FIRST_ADDRESS, MSG_AMOUNT_FREE,
    MSG_LARGEST_BLOCK, MSG_NUM_CHUNKS, MSG_NODE_NAME,
    MSG_MEMORY_SPEED
};

enum {
    DRV_ERRORS, DRV_UNIT, DRV_STATE, DRV_TOTAL, DRV_USED, DRV_BLOCK_SIZE,
    DRV_DISK_TYPE, DRV_VOLUME, DRV_DEVICE, DRV_SURFACES, DRV_SECTORS,
    DRV_RESERVED, DRV_LOW_CYL, DRV_HIGH_CYL, DRV_BUFFERS, DRV_SPEED,
    DRV_COUNT
};

static const LocaleStringID drive_rows[DRV_COUNT] = {
    MSG_DISK_ERRORS, MSG_UNIT_NUMBER, MSG_DISK_STATE,
    MSG_TOTAL_BLOCKS, MSG_BLOCKS_USED, MSG_BYTES_PER_BLOCK,
    MSG_DISK_TYPE, MSG_VOLUME_NAME, MSG_DEVICE_NAME,
    MSG_SURFACES, MSG_SECTORS_PER_SIDE, MSG_RESERVED_BLOCKS,
    MSG_LOWEST_CYLINDER, MSG_HIGHEST_CYLINDER,
    MSG_NUM_BUFFERS, MSG_SPEED
};

/* Objects; all of them are owned by mui_app */
static Object *mui_app;
static struct MUI_CustomClass *you_gauge_class;
static Object *main_window;
static Object *scsi_window;
static Object *measuring_window;
static Object *status_text;

static Object *software_values[SOFTWARE_OVERVIEW_MAX_ROWS];
static Object *software_cycle;
static Object *software_pages;
static Object *software_list_obj;
static Object *mmu_list_obj;

static Object *hardware_values[HARDWARE_COUNT][MAX_HARDWARE_INFO_ROWS];
static Object *cache_checks[CACHE_SETTING_COUNT];
static Object *register_group;
static Object *hardware_register;
static Object *clock_date;
static Object *clock_time;

static Object *speed_factors[NUM_SPEED_ROWS];
static Object *speed_gauges[NUM_SPEED_ROWS];
static Object *scale_cycle;
static Object *benchmark_button;
static Object *dhrystones_value;
static Object *mips_value;
static Object *mflops_value;
static Object *mem_speed_values[3];     /* Chip, Fast, ROM */

static Object *memory_list_obj;
static Object *memory_values[MEM_COUNT];
static Object *memory_speed_button;

static Object *drive_list_obj;
static Object *drive_values[DRV_COUNT];
static Object *drive_speed_button;
static Object *drive_scsi_button;

static Object *board_list_obj;
static Object *board_list_view;
static Object *board_cycle;
static Object *board_details_button;
static Object *board_details_window;
static Object *board_details_fields;
static Object *board_detail_labels[BOARD_FIELD_COUNT];
static Object *board_detail_values[BOARD_FIELD_COUNT];
static Object *board_details_close_button;
static Object *no_boards_text;

static Object *scsi_title;
static Object *scsi_list_obj;
static Object *scsi_close_button;

/* Strings that must outlive the objects that reference them */
static const char *register_titles[PAGE_COUNT + 1];
static const char *hardware_titles[HARDWARE_COUNT + 1];
static const char *software_entries[5];
static const char *scale_entries[3];
static const char *board_entries[BOARD_DISPLAY_COUNT + 1];
static char speed_labels[NUM_REFERENCE_SYSTEMS][24];
static char gauge_texts[NUM_SPEED_ROWS][20];
static char mem_speed_labels[3][16];
static char window_title[64];
static char mmu_help[512];
static struct NewMenu menu[12];

/* Set when any object could not be created */
static BOOL build_failed;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void set_text(Object *obj, const char *text)
{
    if (obj)
        set(obj, MUIA_Text_Contents, (ULONG)(text ? text : ""));
}

static void add_child(Object *group, Object *child)
{
    if (!child) {
        build_failed = TRUE;
        return;
    }
    if (!group) {
        MUI_DisposeObject(child);
        return;
    }
    DoMethod(group, OM_ADDMEMBER, (ULONG)child);
}

static Object *make_value(void)
{
    return TextObject,
        MUIA_Text_PreParse, (ULONG)(MUIX_L MUIX_PH),
        MUIA_Text_Contents, (ULONG)"",
    End;
}

static Object *make_button(LocaleStringID label)
{
    return MUI_MakeObject(MUIO_Button, (ULONG)get_string(label));
}

/*
 * Two-column label/value group. Rows are packed tightly so the tallest
 * pages still fit a 640x256 Workbench screen.
 */
static Object *make_info_group(const char *title, const LocaleStringID *rows,
                               ULONG count, Object **values)
{
    Object *group;
    ULONG i;

    group = MUI_NewObject(MUIC_Group,
        MUIA_Group_Columns, 2,
        MUIA_Group_HorizSpacing, INFO_COLUMN_SPACING,
        MUIA_Group_VertSpacing, 1,
        title ? MUIA_Frame : TAG_IGNORE, MUIV_Frame_Group,
        title ? MUIA_FrameTitle : TAG_IGNORE, (ULONG)title,
        title ? MUIA_Background : TAG_IGNORE, MUII_GroupBack,
        TAG_DONE);

    if (!group)
        build_failed = TRUE;

    for (i = 0; i < count; i++) {
        values[i] = make_value();
        add_child(group, MUI_MakeObject(MUIO_Label,
                                        (ULONG)get_string(rows[i]), 0));
        add_child(group, values[i]);
    }

    return group;
}

static Object *make_list(Object **list, const char *format,
                         struct Hook *display, BOOL input, BOOL fixed_font)
{
    return ListviewObject,
        MUIA_Listview_Input, input,
        MUIA_Listview_List, *list = ListObject,
            MUIA_Frame, input ? MUIV_Frame_InputList : MUIV_Frame_ReadList,
            MUIA_List_Format, (ULONG)format,
            MUIA_List_Title, display != NULL,
            display ? MUIA_List_DisplayHook : TAG_IGNORE, (ULONG)display,
            fixed_font ? MUIA_Font : TAG_IGNORE, MUIV_Font_Fixed,
        End,
    End;
}

static void notify_return(Object *obj, ULONG attr, ULONG value, ULONG id)
{
    DoMethod(obj, MUIM_Notify, attr, value,
             (ULONG)mui_app, 2, MUIM_Application_ReturnID, id);
}

static void set_sleep(BOOL sleep)
{
    set(mui_app, MUIA_Application_Sleep, sleep);
}

/* Gauge.mui has no per-object fill color in MUI 3.8. Keep its sizing and
 * value handling, and draw only the "You" gauge with our own pen. */
struct YouGaugeData {
    LONG pen;
};

/* A custom-class dispatcher receives register arguments directly. MUI owns
 * the class's dispatcher hook; unlike list hooks, it must not be replaced. */
static ULONG you_gauge_dispatch(
    struct IClass *cl __asm("a0"), Object *obj __asm("a2"), Msg msg __asm("a1"))
{
    struct YouGaugeData *data = INST_DATA(cl, obj);
    ULONG result;

    switch (msg->MethodID) {
        case MUIM_Setup: {
            static const struct MUI_PenSpec red = { "rdddddddd,44444444,44444444" };
            data->pen = -1;
            if (!DoSuperMethodA(cl, obj, msg))
                return FALSE;
            data->pen = MUI_ObtainPen(muiRenderInfo(obj), &red, 0);
            return TRUE;
        }
        case MUIM_Cleanup:
            if (data->pen != -1) {
                MUI_ReleasePen(muiRenderInfo(obj), data->pen);
                data->pen = -1;
            }
            break;
        case MUIM_Draw: {
            ULONG current = 0;
            LONG width, length;
            struct TextExtent extent;

            result = DoSuperMethodA(cl, obj, msg);
            if (data->pen == -1 ||
                !(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
                return result;

            get(obj, MUIA_Gauge_Current, &current);
            if (current > 1000)
                current = 1000;
            width = _mwidth(obj) * current / 1000;
            DoMethod(obj, MUIM_DrawBackground, _mleft(obj), _mtop(obj),
                     _mwidth(obj), _mheight(obj), _mleft(obj), _mtop(obj), 0);
            SetDrMd(_rp(obj), JAM1);
            if (width > 0) {
                SetAPen(_rp(obj), MUIPEN(data->pen));
                RectFill(_rp(obj), _mleft(obj), _mtop(obj),
                         _mleft(obj) + width - 1, _mbottom(obj));
            }
            SetFont(_rp(obj), _font(obj));
            length = TextFit(_rp(obj), gauge_texts[0], strlen(gauge_texts[0]),
                             &extent, NULL, 1, _mwidth(obj), _mheight(obj));
            if (length > 0) {
                SetAPen(_rp(obj), _pens(obj)[MPEN_SHINE]);
                Move(_rp(obj), _mleft(obj) + (_mwidth(obj) - extent.te_Width) / 2,
                     _mtop(obj) + (_mheight(obj) - _font(obj)->tf_YSize) / 2 +
                     _font(obj)->tf_Baseline);
                Text(_rp(obj), gauge_texts[0], length);
            }
            return result;
        }
    }
    return DoSuperMethodA(cl, obj, msg);
}

/* ------------------------------------------------------------------ */
/* List display hooks                                                  */
/* ------------------------------------------------------------------ */

static ULONG software_display(struct Hook *hook, APTR array, APTR message)
{
    static char address[12];
    static char version[16];
    char **columns = (char **)array;
    SoftwareEntry *entry = (SoftwareEntry *)message;

    (void)hook;

    if (!entry) {
        columns[0] = (char *)get_string(MSG_NAME);
        columns[1] = (char *)get_string(MSG_LOCATION);
        columns[2] = (char *)get_string(MSG_ADDRESS);
        columns[3] = (char *)get_string(MSG_VERSION);
        return 0;
    }

    snprintf(address, sizeof(address), "$%08lX",
             (unsigned long)entry->address);
    snprintf(version, sizeof(version), "V%u.%u",
             entry->version, entry->revision);
    columns[0] = entry->name;
    columns[1] = (char *)get_location_string(entry->location);
    columns[2] = address;
    columns[3] = version;
    return 0;
}

static ULONG memory_display(struct Hook *hook, APTR array, APTR message)
{
    static char start[12];
    static char size[16];
    char **columns = (char **)array;
    MemoryRegion *region = (MemoryRegion *)message;

    (void)hook;

    if (!region) {
        columns[0] = (char *)get_string(MSG_NAME);
        columns[1] = (char *)get_string(MSG_ADDRESS);
        columns[2] = (char *)get_string(MSG_SIZE);
        columns[3] = (char *)get_string(MSG_SCSI_TYPE);
        return 0;
    }

    snprintf(start, sizeof(start), "$%08lX",
             (unsigned long)region->start_address);
    format_size(region->total_size, size, sizeof(size));
    columns[0] = region->node_name;
    columns[1] = start;
    columns[2] = size;
    columns[3] = region->type_string;
    return 0;
}

static ULONG drive_display(struct Hook *hook, APTR array, APTR message)
{
    char **columns = (char **)array;
    DriveInfo *drive = (DriveInfo *)message;

    (void)hook;

    if (!drive) {
        columns[0] = (char *)get_string(MSG_DRIVE);
        columns[1] = (char *)get_string(MSG_VOLUME);
        return 0;
    }

    columns[0] = drive->device_name;
    columns[1] = drive->volume_name;
    return 0;
}

/* Mirrors the NAMES/DEC/HEX columns of the classic boards view. */
static ULONG board_display(struct Hook *hook, APTR array, APTR message)
{
    static char product[64];
    static char manufacturer[64];
    static char detail[32];
    char **columns = (char **)array;
    BoardInfo *board = (BoardInfo *)message;

    (void)hook;

    if (!board) {
        columns[0] = (char *)get_string(MSG_BOARD_ADDRESS);
        columns[1] = (char *)get_string(MSG_BOARD_SIZE);
        columns[2] = (char *)get_string(MSG_BOARD_TYPE);
        columns[3] = (char *)get_string(MSG_PRODUCT);
        columns[4] = (char *)get_string(MSG_MANUFACTURER);
        columns[5] = (char *)get_string(MSG_SERIAL_NO);
        return 0;
    }

    columns[0] = board->address_string;
    columns[1] = board->size_string;
    columns[2] = (char *)get_board_type_string(board->board_type);
    columns[3] = (char *)format_board_field(board, app->board_display,
        BOARD_FIELD_PRODUCT, product, sizeof(product));
    columns[4] = (char *)format_board_field(board, app->board_display,
        BOARD_FIELD_MANUFACTURER, manufacturer, sizeof(manufacturer));
    columns[5] = (char *)format_board_field(board, app->board_display,
        BOARD_FIELD_SERIAL, detail, sizeof(detail));
    return 0;
}

static ULONG scsi_display(struct Hook *hook, APTR array, APTR message)
{
    static char id[4];
    static char max_blocks[12];
    static char real_size[16];
    static char format_size[16];
    char **columns = (char **)array;
    ScsiDeviceInfo *dev = (ScsiDeviceInfo *)message;

    (void)hook;

    if (!dev) {
        columns[0] = (char *)get_string(MSG_SCSI_ID);
        columns[1] = (char *)get_string(MSG_SCSI_TYPE);
        columns[2] = (char *)get_string(MSG_SCSI_MANUF);
        columns[3] = (char *)get_string(MSG_SCSI_MODEL);
        columns[4] = (char *)get_string(MSG_SCSI_REV);
        columns[5] = (char *)get_string(MSG_SCSI_MAXBLOCKS);
        columns[6] = (char *)get_string(MSG_SCSI_ANSI);
        columns[7] = (char *)get_string(MSG_SCSI_REAL);
        columns[8] = (char *)get_string(MSG_SCSI_FORMAT);
        return 0;
    }

    snprintf(id, sizeof(id), "%d", dev->target_id);
    snprintf(max_blocks, sizeof(max_blocks), "%lu",
             (unsigned long)dev->max_blocks);
    format_size_mb(dev->real_size_mb, real_size, sizeof(real_size));
    format_size_mb(dev->format_size_mb, format_size, sizeof(format_size));

    columns[0] = id;
    columns[1] = (char *)get_scsi_type_string(dev->device_type);
    columns[2] = dev->manufacturer;
    columns[3] = dev->model;
    columns[4] = dev->revision;
    columns[5] = max_blocks;
    columns[6] = (char *)get_scsi_ansi_string(dev->ansi_version);
    columns[7] = real_size;
    columns[8] = format_size;
    return 0;
}

/*
 * HookEntry (amiga.lib) moves the register arguments onto the stack.
 * Cast to the field type: HOOKFUNC's return type differs between NDKs.
 */
#define DISPLAY_HOOK(name, func) \
    static struct Hook name = { { NULL, NULL }, (ULONG (*)())HookEntry, \
                                (ULONG (*)())func, NULL }

DISPLAY_HOOK(software_hook, software_display);
DISPLAY_HOOK(memory_hook, memory_display);
DISPLAY_HOOK(drive_hook, drive_display);
DISPLAY_HOOK(board_hook, board_display);
DISPLAY_HOOK(scsi_hook, scsi_display);

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */

static Object *make_software_page(void)
{
    Object *overview;
    ULONG i;

    overview = MUI_NewObject(MUIC_Group,
        MUIA_Group_Columns, 2,
        MUIA_Group_HorizSpacing, INFO_COLUMN_SPACING,
        MUIA_Group_VertSpacing, 1,
        GroupFrame,
        TAG_DONE);
    if (!overview)
        build_failed = TRUE;

    for (i = 0; i < software_overview_count(); i++) {
        software_values[i] = make_value();
        add_child(overview, MUI_MakeObject(MUIO_Label,
            (ULONG)get_string(software_overview_label(i)), 0));
        add_child(overview, software_values[i]);
    }

    software_entries[0] = get_string(MSG_LIBRARIES);
    software_entries[1] = get_string(MSG_DEVICES);
    software_entries[2] = get_string(MSG_RESOURCES);
    software_entries[3] = get_string(MSG_MMU_ENTRIES);
    software_entries[4] = NULL;

    snprintf(mmu_help, sizeof(mmu_help), "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
             get_string(MSG_MMU_ADDRESS_HINT),
             get_string(MSG_MMU_FLAGS1_HINT), get_string(MSG_MMU_FLAGS2_HINT),
             get_string(MSG_MMU_FLAGS3_HINT), get_string(MSG_MMU_FLAGS4_HINT),
             get_string(MSG_MMU_FLAGS5_HINT), get_string(MSG_MMU_FLAGS6_HINT),
             get_string(MSG_MMU_FLAGS7_HINT));

    return VGroup,
        Child, overview,
        Child, HGroup,
            Child, HSpace(0),
            Child, software_cycle = MUI_MakeObject(MUIO_Cycle, 0,
                                                   (ULONG)software_entries),
        End,
        Child, software_pages = PageGroup,
            Child, make_list(&software_list_obj, "BAR,BAR,BAR,",
                             &software_hook, FALSE, FALSE),
            Child, VGroup,
                MUIA_ShortHelp, (ULONG)mmu_help,
                Child, make_list(&mmu_list_obj, "", NULL, FALSE, TRUE),
            End,
        End,
    End;
}

/* Hardware detection fixes row membership before either UI opens. Values
 * (frequencies, cache state and RTC) are refreshed without rebuilding widgets. */
typedef struct {
    HardwareType page;
    ULONG index;
    Object *root;
    Object *section;
    UWORD group;
} HardwarePage;

static void make_hardware_row(const HardwareInfoRow *row, void *data)
{
    HardwarePage *page = data;
    Object *value;

    if (page->index >= MAX_HARDWARE_INFO_ROWS) {
        build_failed = TRUE;
        return;
    }
    if (!page->section) {
        page->section = ColGroup(2),
            MUIA_Group_HorizSpacing, INFO_COLUMN_SPACING,
            MUIA_Group_VertSpacing, 1,
        End;
        add_child(page->root, page->section);
    } else if (page->group != row->group) {
        /* Keep section breaks inside one grid so every value starts at
         * the same column, even when sections have different labels. */
        add_child(page->section, VSpace(4));
        add_child(page->section, VSpace(4));
    }
    page->group = row->group;
    add_child(page->section, MUI_MakeObject(MUIO_Label,
                                           (ULONG)row->label, 0));
    if (row->control != CACHE_NONE) {
        value = MUI_MakeObject(MUIO_Checkmark, 0);
        cache_checks[row->control] = value;
    } else {
        value = make_value();
        if (row->label == get_string(MSG_RTC_DATE)) clock_date = value;
        if (row->label == get_string(MSG_RTC_TIME)) clock_time = value;
    }
    hardware_values[page->page][page->index++] = value;
    if (row->control != CACHE_NONE) {
        /* A fixed-width checkmark must not cap the section or page width. */
        if (!value)
            build_failed = TRUE;
        add_child(page->section, HGroup,
            Child, value,
            Child, HSpace(0),
        End);
    } else {
        add_child(page->section, value);
    }
}

static Object *make_hardware_page(HardwareType type)
{
    HardwarePage page = { type, 0, NULL, NULL, 0 };
    page.root = VirtgroupObject,
        VirtualFrame,
    End;
    if (!page.root) {
        build_failed = TRUE;
        return NULL;
    }
    visit_hardware_rows(type, make_hardware_row, &page);
    if (!page.index)
        add_child(page.root, TextObject,
            MUIA_Text_Contents, (ULONG)get_string(MSG_NA),
        End);
    add_child(page.root, VSpace(0));
    return ScrollgroupObject,
        MUIA_Scrollgroup_Contents, (ULONG)page.root,
    End;
}

static void update_hardware_row(const HardwareInfoRow *row, void *data)
{
    HardwarePage *page = data;
    Object *value;
    if (page->index >= MAX_HARDWARE_INFO_ROWS) return;
    value = hardware_values[page->page][page->index++];
    if (row->control != CACHE_NONE)
        nnset(value, MUIA_Selected, cache_setting_enabled(row->control));
    else
        set_text(value, row->value);
}

static void update_hardware_values(void)
{
    HardwareType type;
    for (type = HARDWARE_STD; type < HARDWARE_COUNT; type++) {
        HardwarePage page = { type, 0, NULL, NULL, 0 };
        visit_hardware_rows(type, update_hardware_row, &page);
    }
}

static void update_clock_time(void)
{
    char values[2][24];
    format_clock_values(values);
    set_text(clock_date, values[0]);
    set_text(clock_time, values[1]);
}

static Object *make_speed_page(void)
{
    Object *bars;
    Object *results;
    ULONG i;
    char *label;
    char header[64];

    bars = MUI_NewObject(MUIC_Group,
        MUIA_Group_Columns, 3,
        MUIA_Group_VertSpacing, 2,
        GroupFrameT((ULONG)get_string(MSG_SPEED_COMPARISONS)),
        TAG_DONE);
    if (!bars)
        build_failed = TRUE;

    for (i = 0; i < NUM_SPEED_ROWS; i++) {
        const char *text;
        struct TagItem gauge_tags[] = {
            { MUIA_Frame, MUIV_Frame_Gauge },
            { MUIA_Gauge_Horiz, TRUE },
            { MUIA_Gauge_Max, 1000 },
            { MUIA_Gauge_InfoText, (ULONG)"" },
            { TAG_DONE, 0 }
        };

        if (i == 0) {
            text = get_string(MSG_REF_YOU);
        } else {
            format_reference_label(speed_labels[i - 1],
                                   sizeof(speed_labels[i - 1]),
                                   &reference_systems[i - 1]);
            text = speed_labels[i - 1];
        }

        speed_factors[i] = make_value();
        if (i == 0 && you_gauge_class) {
            speed_gauges[i] = NewObjectA(you_gauge_class->mcc_Class, NULL,
                                         gauge_tags);
            if (!speed_gauges[i])
                debug(XSYSINFO_NAME ": Could not create the custom You gauge; using standard gauge\n");
        }
        if (!speed_gauges[i])
            speed_gauges[i] = MUI_NewObjectA(MUIC_Gauge, gauge_tags);

        add_child(bars, MUI_MakeObject(MUIO_Label, (ULONG)text,
                                       MUIO_Label_LeftAligned));
        add_child(bars, speed_factors[i]);
        add_child(bars, speed_gauges[i]);
    }

    scale_entries[0] = get_string(MSG_EXPAND);
    scale_entries[1] = get_string(MSG_SHRINK);
    scale_entries[2] = NULL;

    /* The catalog holds the three memory speed headings in one string. */
    copy_string(header, get_string(MSG_MEM_SPEED_HEADER), sizeof(header));
    label = header;
    for (i = 0; i < 3; i++) {
        char *next;

        label += strspn(label, " ");
        next = label + strcspn(label, " ");
        if (*next)
            *next++ = '\0';
        copy_string(mem_speed_labels[i], label, sizeof(mem_speed_labels[i]));
        label = next;
    }

    results = ColGroup(6),
        GroupFrameT((ULONG)get_string(MSG_RESULTS_TITLE)),
        MUIA_Group_HorizSpacing, INFO_COLUMN_SPACING,
        MUIA_Group_VertSpacing, 1,
        Child, MUI_MakeObject(MUIO_Label, (ULONG)get_string(MSG_DHRYSTONES), 0),
        Child, dhrystones_value = make_value(),
        Child, MUI_MakeObject(MUIO_Label, (ULONG)get_string(MSG_MIPS), 0),
        Child, mips_value = make_value(),
        Child, MUI_MakeObject(MUIO_Label, (ULONG)get_string(MSG_MFLOPS), 0),
        Child, mflops_value = make_value(),
        Child, MUI_MakeObject(MUIO_Label, (ULONG)mem_speed_labels[0], 0),
        Child, mem_speed_values[0] = make_value(),
        Child, MUI_MakeObject(MUIO_Label, (ULONG)mem_speed_labels[1], 0),
        Child, mem_speed_values[1] = make_value(),
        Child, MUI_MakeObject(MUIO_Label, (ULONG)mem_speed_labels[2], 0),
        Child, mem_speed_values[2] = make_value(),
    End;

    return VGroup,
        Child, bars,
        Child, results,
        Child, HGroup,
            Child, benchmark_button = make_button(MSG_RUN_BENCHMARKS),
            Child, HSpace(0),
            Child, scale_cycle = MUI_MakeObject(MUIO_Cycle, 0,
                                                (ULONG)scale_entries),
        End,
    End;
}

static Object *make_memory_page(void)
{
    return VGroup,
        Child, make_list(&memory_list_obj, "BAR,BAR,BAR,", &memory_hook,
                         TRUE, FALSE),
        Child, VGroup,
            GroupFrame,
            MUIA_Background, MUII_GroupBack,
            Child, make_info_group(NULL, memory_rows, MEM_COUNT, memory_values),
            Child, HGroup,
                Child, memory_speed_button = make_button(MSG_MEASURE_SPEED),
                Child, HSpace(0),
            End,
        End,
    End;
}

static Object *make_drives_page(void)
{
    return HGroup,
        Child, VGroup,
            MUIA_HorizWeight, 30,
            Child, make_list(&drive_list_obj, "BAR,", &drive_hook,
                             TRUE, FALSE),
        End,
        Child, VGroup,
            MUIA_HorizWeight, 70,
            Child, make_info_group(NULL, drive_rows, DRV_COUNT, drive_values),
            Child, VSpace(0),
            Child, HGroup,
                Child, drive_speed_button = make_button(MSG_MEASURE_SPEED),
                Child, drive_scsi_button = make_button(MSG_SCSI_DEVICES),
            End,
        End,
    End;
}

static Object *make_boards_page(void)
{
    board_entries[0] = get_string(MSG_BOARD_NAMES);
    board_entries[1] = get_string(MSG_BOARD_DECIMAL);
    board_entries[2] = get_string(MSG_BOARD_HEX);
    board_entries[3] = NULL;

    return VGroup,
        Child, board_list_view = make_list(&board_list_obj, "BAR,BAR,BAR,BAR,BAR,",
                         &board_hook, TRUE, FALSE),
        Child, no_boards_text = TextObject,
            MUIA_Text_PreParse, (ULONG)"\33c",
            MUIA_Text_Contents,
                (ULONG)get_string(MSG_BOARDS_NO_BOARDS_FOUND),
        End,
        Child, HGroup,
            Child, HSpace(0),
            Child, board_cycle = MUI_MakeObject(MUIO_Cycle, 0,
                                                (ULONG)board_entries),
            Child, board_details_button = make_button(MSG_BOARD_DETAILS),
        End,
    End;
}

static Object *make_board_details_window(void)
{
    Object *labels, *values;
    ULONG i;

    /* Separate columns let MUI 3.8 hide PCI's unused rows safely. */
    labels = VGroup, MUIA_Group_VertSpacing, 1, End;
    values = VGroup, MUIA_Group_VertSpacing, 1, End;
    if (!labels || !values) build_failed = TRUE;
    for (i = 0; i < BOARD_FIELD_COUNT; i++) {
        board_detail_labels[i] = TextObject,
            MUIA_Text_Contents, (ULONG)get_string(board_field_label(i, BOARD_ZORRO_II)),
        End;
        board_detail_values[i] = make_value();
        add_child(labels, board_detail_labels[i]);
        add_child(values, board_detail_values[i]);
    }
    /* Use the current fields' natural size instead of screen percentages. */
    return WindowObject,
        MUIA_Window_Title, (ULONG)get_string(MSG_BOARD_DETAILS_TITLE),
        WindowContents, VGroup,
            Child, board_details_fields = HGroup,
                MUIA_Group_HorizSpacing, INFO_COLUMN_SPACING,
                Child, labels,
                Child, values,
            End,
            Child, board_details_close_button = make_button(MSG_CLOSE),
        End,
    End;
}

static Object *make_scsi_window(void)
{
    return WindowObject,
        MUIA_Window_Title, (ULONG)get_string(MSG_SCSI_INFO),
        MUIA_Window_ID, MAKE_ID('S', 'C', 'S', 'I'),
        /* Leave room for the nine columns and several device rows. */
        MUIA_Window_Width, MUIV_Window_Width_Visible(90),
        MUIA_Window_Height, MUIV_Window_Height_Visible(50),
        WindowContents, VGroup,
            Child, scsi_title = TextObject,
                TextFrame,
                MUIA_Background, MUII_TextBack,
                MUIA_Text_Contents, (ULONG)"",
            End,
            Child, make_list(&scsi_list_obj, "BAR,BAR,BAR,BAR,BAR,BAR,BAR,BAR,",
                             &scsi_hook, FALSE, FALSE),
            Child, scsi_close_button = make_button(MSG_CLOSE),
        End,
    End;
}

static void build_menu(void)
{
    static const struct {
        UBYTE type;
        LocaleStringID label;
        const char *key;
        ULONG id;
    } items[] = {
        { NM_TITLE, MSG_PROJECT_MENU,   NULL, 0 },
        { NM_ITEM,  MSG_ABOUT,     "?",  ID_ABOUT },
        { NM_ITEM,  MSG_ABOUT_MUI, NULL, ID_ABOUT_MUI },
        { NM_ITEM,  MSG_COUNT,              NULL, 0 },
        { NM_ITEM,  MSG_RUN_BENCHMARKS, "R",  ID_BENCHMARK },
        { NM_ITEM,  MSG_SAVE_REPORT,      "S",  ID_SAVE },
        { NM_ITEM,  MSG_COUNT,              NULL, 0 },
        { NM_ITEM,  MSG_QUIT,      "Q",
          (ULONG)MUIV_Application_ReturnID_Quit },
        { NM_TITLE, MSG_SETTINGS_MENU,  NULL, 0 },
        { NM_ITEM,  MSG_MUI_SETTINGS, NULL, ID_MUI_PREFS },
    };
    ULONG i;

    memset(menu, 0, sizeof(menu));
    for (i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        menu[i].nm_Type = items[i].type;
        /* MSG_COUNT marks a separator bar */
        menu[i].nm_Label = items[i].label == MSG_COUNT ? NM_BARLABEL :
                           (STRPTR)get_string(items[i].label);
        menu[i].nm_CommKey = (STRPTR)items[i].key;
        menu[i].nm_UserData = (APTR)items[i].id;
    }
    menu[i].nm_Type = NM_END;
}

static BOOL create_application(const char *version_string)
{
    Object *save_button;
    Object *quit_button;
    ULONG i;

    build_failed = FALSE;
    build_menu();

    you_gauge_class = MUI_CreateCustomClass(NULL, MUIC_Gauge, NULL,
                                           sizeof(struct YouGaugeData),
                                           (APTR)you_gauge_dispatch);
    if (!you_gauge_class)
        debug(XSYSINFO_NAME ": Could not create the custom gauge class; using standard gauge\n");

    snprintf(window_title, sizeof(window_title), "%s %s",
             XSYSINFO_NAME, XSYSINFO_VERSION);

    register_titles[PAGE_SOFTWARE] = get_string(MSG_SOFTWARE_TITLE);
    register_titles[PAGE_HARDWARE] = get_string(MSG_HARDWARE_TITLE);
    hardware_titles[HARDWARE_STD] = get_string(MSG_HARDWARE_STD);
    hardware_titles[HARDWARE_CPU] = get_string(MSG_HARDWARE_CPU);
    hardware_titles[HARDWARE_EXT] = get_string(MSG_CHIPSET_TITLE);
    hardware_titles[HARDWARE_SCSI] = get_string(MSG_HARDWARE_SCSI);
    hardware_titles[HARDWARE_CLOCK] = get_string(MSG_CLOCK);
    hardware_titles[HARDWARE_COUNT] = NULL;
    register_titles[PAGE_SPEED] = get_string(MSG_SPEED_TITLE);
    register_titles[PAGE_MEMORY] = get_string(MSG_MEMORY_TITLE);
    register_titles[PAGE_DRIVES] = get_string(MSG_DRIVES_TITLE);
    register_titles[PAGE_BOARDS] = get_string(MSG_BOARDS_TITLE);
    register_titles[PAGE_COUNT] = NULL;

    mui_app = ApplicationObject,
        MUIA_Application_Title, (ULONG)XSYSINFO_NAME,
        MUIA_Application_Version, (ULONG)version_string,
        MUIA_Application_Copyright, (ULONG)"(C) 2025-2026 Stefan Reinauer",
        MUIA_Application_Author, (ULONG)"Stefan Reinauer",
        MUIA_Application_Description, (ULONG)get_string(MSG_TAGLINE),
        MUIA_Application_Base, (ULONG)"XSYSINFO",
        MUIA_Application_Menustrip,
            (ULONG)MUI_MakeObject(MUIO_MenustripNM, (ULONG)menu, 0),

        SubWindow, main_window = WindowObject,
            MUIA_Window_Title, (ULONG)window_title,
            /* Keep Escape available to dialogs without quitting the app. */
            MUIA_Window_DisableKeys, MUIKEYF_WINDOW_CLOSE,
            MUIA_Window_ID, MAKE_ID('M', 'A', 'I', 'N'),
            WindowContents, VGroup,
                Child, register_group = RegisterGroup(register_titles),
                    MUIA_Register_Frame, TRUE,
                    Child, make_software_page(),
                    Child, hardware_register = RegisterGroup(hardware_titles),
                        MUIA_Register_Frame, TRUE,
                        Child, make_hardware_page(HARDWARE_STD),
                        Child, make_hardware_page(HARDWARE_CPU),
                        Child, make_hardware_page(HARDWARE_EXT),
                        Child, make_hardware_page(HARDWARE_SCSI),
                        Child, make_hardware_page(HARDWARE_CLOCK),
                    End,
                    Child, make_speed_page(),
                    Child, make_memory_page(),
                    Child, make_drives_page(),
                    Child, make_boards_page(),
                End,
                Child, HGroup,
                    Child, status_text = TextObject,
                        TextFrame,
                        MUIA_Background, MUII_TextBack,
                        MUIA_Text_Contents, (ULONG)"",
                    End,
                    Child, save_button = make_button(MSG_SAVE_REPORT),
                    Child, quit_button = make_button(MSG_QUIT),
                End,
            End,
        End,

        SubWindow, board_details_window = make_board_details_window(),
        SubWindow, scsi_window = make_scsi_window(),
        SubWindow, measuring_window = WindowObject,
            MUIA_Window_Title, (ULONG)XSYSINFO_NAME,
            MUIA_Window_CloseGadget, FALSE,
            MUIA_Window_SizeGadget, FALSE,
            MUIA_Window_DepthGadget, FALSE,
            WindowContents, VGroup,
                MUIA_InnerLeft, 16,
                MUIA_InnerRight, 16,
                MUIA_InnerTop, 12,
                MUIA_InnerBottom, 12,
                Child, TextObject,
                    MUIA_Text_PreParse, (ULONG)"\33c",
                    MUIA_Text_Contents, (ULONG)get_string(MSG_MEASURING_SPEED),
                End,
            End,
        End,
    End;

    if (!mui_app || build_failed) {
        if (mui_app) {
            MUI_DisposeObject(mui_app);
            mui_app = NULL;
        }
        return FALSE;
    }

    DoMethod(main_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)mui_app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    notify_return(quit_button, MUIA_Pressed, FALSE,
                  (ULONG)MUIV_Application_ReturnID_Quit);
    notify_return(save_button, MUIA_Pressed, FALSE, ID_SAVE);
    notify_return(benchmark_button, MUIA_Pressed, FALSE, ID_BENCHMARK);
    notify_return(software_cycle, MUIA_Cycle_Active, MUIV_EveryTime,
                  ID_SOFTWARE_PAGE);
    notify_return(scale_cycle, MUIA_Cycle_Active, MUIV_EveryTime, ID_SCALE);
    for (i = CACHE_ICACHE; i < CACHE_SETTING_COUNT; i++) {
        if (cache_checks[i])
            notify_return(cache_checks[i], MUIA_Selected, MUIV_EveryTime,
                      ID_CACHE_BASE + i);
    }
    notify_return(memory_list_obj, MUIA_List_Active, MUIV_EveryTime,
                  ID_MEMORY_SELECT);
    notify_return(memory_speed_button, MUIA_Pressed, FALSE, ID_MEMORY_SPEED);
    notify_return(drive_list_obj, MUIA_List_Active, MUIV_EveryTime,
                  ID_DRIVE_SELECT);
    notify_return(drive_speed_button, MUIA_Pressed, FALSE, ID_DRIVE_SPEED);
    notify_return(drive_scsi_button, MUIA_Pressed, FALSE, ID_DRIVE_SCSI);
    notify_return(board_cycle, MUIA_Cycle_Active, MUIV_EveryTime,
                  ID_BOARD_DISPLAY);
    notify_return(board_list_obj, MUIA_List_Active, MUIV_EveryTime, ID_BOARD_SELECT);
    notify_return(board_list_view, MUIA_Listview_DoubleClick, TRUE, ID_BOARD_DETAILS);
    notify_return(board_details_button, MUIA_Pressed, FALSE, ID_BOARD_DETAILS);
    notify_return(board_details_window, MUIA_Window_CloseRequest, TRUE,
                  ID_BOARD_DETAILS_CLOSE);
    notify_return(board_details_close_button, MUIA_Pressed, FALSE,
                  ID_BOARD_DETAILS_CLOSE);

    DoMethod(scsi_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)mui_app, 2, MUIM_Application_ReturnID, ID_SCSI_CLOSE);
    notify_return(scsi_close_button, MUIA_Pressed, FALSE, ID_SCSI_CLOSE);

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Updating values                                                     */
/* ------------------------------------------------------------------ */

static void update_software_overview(void)
{
    char buffer[80];
    ULONG i;

    for (i = 0; i < software_overview_count(); i++) {
        set_text(software_values[i],
                 format_software_overview_value(i, buffer, sizeof(buffer)));
    }
}

static void show_software_list(ULONG index)
{
    SoftwareType type = (SoftwareType)(SOFTWARE_LIBRARIES + index);
    SoftwareList *list = get_software_list(type);
    Object *target = type == SOFTWARE_MMU ? mmu_list_obj : software_list_obj;
    ULONG i;

    set(software_pages, MUIA_Group_ActivePage, type == SOFTWARE_MMU);

    set(target, MUIA_List_Quiet, TRUE);
    DoMethod(target, MUIM_List_Clear);
    for (i = 0; list && i < list->count; i++) {
        /* The MMU page lists preformatted lines as plain strings. */
        APTR entry = type == SOFTWARE_MMU ? (APTR)list->entries[i].name :
                                            (APTR)&list->entries[i];
        DoMethod(target, MUIM_List_InsertSingle, (ULONG)entry,
                 MUIV_List_Insert_Bottom);
    }
    set(target, MUIA_List_Quiet, FALSE);
}

static void format_mb_per_sec(ULONG bytes_per_sec, char *buffer, size_t size)
{
    char scaled[16];

    if (bench_results.benchmarks_valid && bytes_per_sec > 0) {
        format_scaled(scaled, sizeof(scaled), bytes_per_sec / 10000, TRUE);
        snprintf(buffer, size, "%s %s", scaled,
                 get_string(MSG_MEM_SPEED_UNIT));
    } else {
        copy_string(buffer, get_string(MSG_NA), size);
    }
}

static void update_speed_values(void)
{
    ULONG max_value = speed_scale_max(app->bar_scale);
    char buffer[32];
    ULONG i;

    for (i = 0; i < NUM_SPEED_ROWS; i++) {
        ULONG value;
        ULONG width;
        BOOL overflow;

        if (i == 0) {
            value = bench_results.benchmarks_valid ?
                    bench_results.dhrystones : 0;
            buffer[0] = '\0';
        } else {
            const ReferenceSystem *ref = &reference_systems[i - 1];

            value = ref->dhrystones;
            if (bench_results.benchmarks_valid && ref->dhrystones > 0) {
                ULONG factor_x100 =
                    (bench_results.dhrystones * 100) / ref->dhrystones;
                format_scaled(buffer, sizeof(buffer), factor_x100,
                              factor_x100 >= 100000);
            } else {
                buffer[0] = '\0';
            }
        }
        set_text(speed_factors[i], buffer);

        width = scale_speed_value(value, max_value, 1000, app->bar_scale);
        overflow = width > 1000 || value > max_value;
        if (width > 1000)
            width = 1000;

        if (value)
            snprintf(gauge_texts[i], sizeof(gauge_texts[i]), "%lu%s",
                     (unsigned long)value, overflow ? " +" : "");
        else
            gauge_texts[i][0] = '\0';

        SetAttrs(speed_gauges[i],
                 MUIA_Gauge_Current, width,
                 MUIA_Gauge_InfoText, (ULONG)gauge_texts[i],
                 TAG_DONE);
    }

    if (bench_results.benchmarks_valid) {
        snprintf(buffer, sizeof(buffer), "%lu",
                 (unsigned long)bench_results.dhrystones);
        set_text(dhrystones_value, buffer);
        format_scaled(buffer, sizeof(buffer), bench_results.mips, TRUE);
        set_text(mips_value, buffer);
    } else {
        set_text(dhrystones_value, get_string(MSG_NA));
        set_text(mips_value, get_string(MSG_NA));
    }

    if (hw_info.fpu_type != FPU_NONE && bench_results.benchmarks_valid &&
        hw_info.fpu_enabled) {
        format_scaled(buffer, sizeof(buffer), bench_results.mflops, TRUE);
        set_text(mflops_value, buffer);
    } else {
        set_text(mflops_value, get_string(MSG_NA));
    }

    format_mb_per_sec(bench_results.chip_speed, buffer, sizeof(buffer));
    set_text(mem_speed_values[0], buffer);
    format_mb_per_sec(bench_results.fast_speed, buffer, sizeof(buffer));
    set_text(mem_speed_values[1], buffer);
    format_mb_per_sec(bench_results.rom_speed, buffer, sizeof(buffer));
    set_text(mem_speed_values[2], buffer);
}

static LONG active_entry(Object *list, ULONG count)
{
    LONG index = MUIV_List_Active_Off;

    get(list, MUIA_List_Active, &index);
    return (index >= 0 && (ULONG)index < count) ? index : -1;
}

static void update_board_details(void)
{
    LONG index = active_entry(board_list_obj, board_list.count);
    const BoardInfo *board;
    ULONG row, count;
    char buffer[64];
    BOOL changing;

    app->selected_board = index;
    set(board_details_button, MUIA_Disabled, index < 0);
    if (index < 0) {
        set(board_details_window, MUIA_Window_Open, FALSE);
        return;
    }
    board = &board_list.boards[index];
    count = board_detail_count(board);
    changing = DoMethod(board_details_fields, MUIM_Group_InitChange);
    for (row = 0; row < BOARD_FIELD_COUNT; row++) {
        set(board_detail_labels[row], MUIA_ShowMe, row < count);
        set(board_detail_values[row], MUIA_ShowMe, row < count);
        if (row < count) {
            set_text(board_detail_labels[row],
                     get_string(board_field_label(row, board->board_type)));
            set_text(board_detail_values[row],
                     format_board_field(board, app->board_display, row,
                                        buffer, sizeof(buffer)));
        } else {
            set_text(board_detail_labels[row], "");
            set_text(board_detail_values[row], "");
        }
    }
    if (changing)
        DoMethod(board_details_fields, MUIM_Group_ExitChange);
}

static void update_memory_details(void)
{
    LONG index = active_entry(memory_list_obj, memory_regions.count);
    MemoryRegion *region;
    char buffer[64];
    ULONG i;

    set(memory_speed_button, MUIA_Disabled, index < 0);
    if (index < 0) {
        for (i = 0; i < MEM_COUNT; i++)
            set_text(memory_values[i], "");
        return;
    }

    refresh_memory_region(index);
    region = &memory_regions.regions[index];

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->start_address);
    set_text(memory_values[MEM_START], buffer);
    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->end_address);
    set_text(memory_values[MEM_END], buffer);
    format_size(region->total_size, buffer, sizeof(buffer));
    set_text(memory_values[MEM_TOTAL], buffer);
    set_text(memory_values[MEM_TYPE], region->type_string);
    snprintf(buffer, sizeof(buffer), "%d", region->priority);
    set_text(memory_values[MEM_PRIORITY], buffer);
    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->lower_bound);
    set_text(memory_values[MEM_LOWER], buffer);
    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->upper_bound);
    set_text(memory_values[MEM_UPPER], buffer);
    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->first_free);
    set_text(memory_values[MEM_FIRST], buffer);
    snprintf(buffer, sizeof(buffer), "%lu Bytes",
             (unsigned long)region->amount_free);
    set_text(memory_values[MEM_FREE], buffer);
    snprintf(buffer, sizeof(buffer), "%lu Bytes",
             (unsigned long)region->largest_block);
    set_text(memory_values[MEM_LARGEST], buffer);
    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)region->num_chunks);
    set_text(memory_values[MEM_CHUNKS], buffer);
    set_text(memory_values[MEM_NODE], region->node_name);
    format_memory_speed(region, buffer, sizeof(buffer));
    set_text(memory_values[MEM_SPEED], buffer);
}

static void update_drive_details(void)
{
    LONG index = active_entry(drive_list_obj, drive_list.count);
    const char *dash = get_string(MSG_DASH_PLACEHOLDER);
    DriveInfo *drive;
    BOOL no_disk;
    char buffer[64];
    ULONG i;

    if (index < 0) {
        for (i = 0; i < DRV_COUNT; i++)
            set_text(drive_values[i], "");
        set(drive_speed_button, MUIA_Disabled, TRUE);
        set(drive_scsi_button, MUIA_Disabled, TRUE);
        return;
    }

    drive = &drive_list.drives[index];
    no_disk = drive->disk_state == DISK_NO_DISK;

    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->disk_errors);
    set_text(drive_values[DRV_ERRORS], buffer);
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->unit_number);
    set_text(drive_values[DRV_UNIT], buffer);
    set_text(drive_values[DRV_STATE],
             no_disk ? dash : get_disk_state_string(drive->disk_state));
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->total_blocks);
    set_text(drive_values[DRV_TOTAL], buffer);

    if (no_disk) {
        set_text(drive_values[DRV_USED], dash);
        set_text(drive_values[DRV_BLOCK_SIZE], dash);
        set_text(drive_values[DRV_DISK_TYPE],
                 get_string(MSG_DISK_NO_DISK_INSERTED));
    } else {
        snprintf(buffer, sizeof(buffer), "%lu",
                 (unsigned long)drive->blocks_used);
        set_text(drive_values[DRV_USED], buffer);
        format_block_size_display(drive, buffer, sizeof(buffer));
        set_text(drive_values[DRV_BLOCK_SIZE], buffer);
        format_filesystem_display(drive, buffer, sizeof(buffer));
        set_text(drive_values[DRV_DISK_TYPE], buffer);
    }

    set_text(drive_values[DRV_VOLUME],
             (no_disk || !drive->volume_name[0]) ? dash : drive->volume_name);
    set_text(drive_values[DRV_DEVICE],
             drive->handler_name[0] ? drive->handler_name : dash);
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->surfaces);
    set_text(drive_values[DRV_SURFACES], buffer);
    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->sectors_per_track);
    set_text(drive_values[DRV_SECTORS], buffer);
    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->reserved_blocks);
    set_text(drive_values[DRV_RESERVED], buffer);
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->low_cylinder);
    set_text(drive_values[DRV_LOW_CYL], buffer);
    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)drive->high_cylinder);
    set_text(drive_values[DRV_HIGH_CYL], buffer);
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)drive->num_buffers);
    set_text(drive_values[DRV_BUFFERS], buffer);
    format_drive_speed(drive, buffer, sizeof(buffer));
    set_text(drive_values[DRV_SPEED], buffer);

    /* Speed needs media and an exec device; see drives_view_update_buttons() */
    set(drive_speed_button, MUIA_Disabled,
        !(drive->handler_name[0] && drive_has_media_evidence(drive)));
    set(drive_scsi_button, MUIA_Disabled, !drive->scsi_supported);
}

static void update_all_values(void)
{
    update_software_overview();
    update_hardware_values();
    update_speed_values();
    update_memory_details();
    update_drive_details();
    update_board_details();
}

static void fill_lists(void)
{
    ULONG i;

    show_software_list(0);

    for (i = 0; i < memory_regions.count; i++) {
        DoMethod(memory_list_obj, MUIM_List_InsertSingle,
                 (ULONG)&memory_regions.regions[i], MUIV_List_Insert_Bottom);
    }
    /* nnset: the selection notifications are already installed. */
    if (memory_regions.count)
        nnset(memory_list_obj, MUIA_List_Active, 0);

    for (i = 0; i < drive_list.count; i++) {
        DoMethod(drive_list_obj, MUIM_List_InsertSingle,
                 (ULONG)&drive_list.drives[i], MUIV_List_Insert_Bottom);
    }
    /* Like the classic view, don't probe the first drive for media here. */
    if (drive_list.count)
        nnset(drive_list_obj, MUIA_List_Active, 0);

    for (i = 0; i < board_list.count; i++) {
        DoMethod(board_list_obj, MUIM_List_InsertSingle,
                 (ULONG)&board_list.boards[i], MUIV_List_Insert_Bottom);
    }
    if (board_list.count)
        nnset(board_list_obj, MUIA_List_Active, 0);
    set(no_boards_text, MUIA_ShowMe, board_list.count == 0);
    set(board_cycle, MUIA_Disabled, board_list.count == 0);
    nnset(board_cycle, MUIA_Cycle_Active, app->board_display);

    nnset(scale_cycle, MUIA_Cycle_Active,
          app->bar_scale == SCALE_SHRINK ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void begin_measurement(void)
{
    set_text(status_text, get_string(MSG_MEASURING_SPEED));
    set(measuring_window, MUIA_Window_RefWindow, main_window);
    set(measuring_window, MUIA_Window_Open, TRUE);
    set_sleep(TRUE);
    /* Finish painting before Forbid() prevents other tasks from running. */
    DoMethod(mui_app, MUIM_Application_InputBuffered);
    WaitTOF();
}

static void end_measurement(void)
{
    set(measuring_window, MUIA_Window_Open, FALSE);
    set_sleep(FALSE);
    set_text(status_text, "");
}

static void run_speed_test(void)
{
    begin_measurement();

    /* Same as the classic interface: keep other tasks out of the timing. */
    Forbid();
    run_benchmarks();
    Permit();

    end_measurement();
    update_hardware_values();
    update_speed_values();
}

static void measure_selected_memory(void)
{
    LONG index = active_entry(memory_list_obj, memory_regions.count);

    if (index < 0)
        return;

    begin_measurement();
    Forbid();
    measure_memory_speed(index);
    Permit();
    end_measurement();
    update_memory_details();
}

static void select_drive(void)
{
    LONG index = active_entry(drive_list_obj, drive_list.count);

    if (index >= 0) {
        set_sleep(TRUE);
        check_disk_present(index);
        set_sleep(FALSE);
    }
    update_drive_details();
    DoMethod(drive_list_obj, MUIM_List_Redraw, MUIV_List_Redraw_Active);
}

static void measure_selected_drive(void)
{
    LONG index = active_entry(drive_list_obj, drive_list.count);

    if (index < 0)
        return;

    begin_measurement();
    measure_drive_speed(index);
    end_measurement();
    update_drive_details();
}

static void show_scsi_devices(void)
{
    LONG index = active_entry(drive_list_obj, drive_list.count);
    DriveInfo *drive;
    ULONG i;

    if (index < 0)
        return;

    drive = &drive_list.drives[index];
    set_sleep(TRUE);
    scan_scsi_devices(drive->handler_name, drive->unit_number);
    set_sleep(FALSE);

    set(scsi_list_obj, MUIA_List_Quiet, TRUE);
    DoMethod(scsi_list_obj, MUIM_List_Clear);
    for (i = 0; i < scsi_device_list.count; i++) {
        if (scsi_device_list.devices[i].is_valid) {
            DoMethod(scsi_list_obj, MUIM_List_InsertSingle,
                     (ULONG)&scsi_device_list.devices[i],
                     MUIV_List_Insert_Bottom);
        }
    }
    set(scsi_list_obj, MUIA_List_Quiet, FALSE);

    set_text(scsi_title, scsi_device_list.count ?
                         scsi_device_list.device_name :
                         get_string(MSG_SCSI_NO_DEVICES));
    set(scsi_window, MUIA_Window_Open, TRUE);
}

static void save_report(void)
{
    struct FileRequester *req;
    struct Window *window = NULL;
    char path[MAX_FILENAME_LEN];
    char message[MAX_FILENAME_LEN + 64];
    BOOL saved;

    req = MUI_AllocAslRequestTags(ASL_FileRequest, TAG_DONE);
    if (!req)
        return;

    get(main_window, MUIA_Window_Window, &window);
    if (MUI_AslRequestTags(req,
            ASLFR_Window, (ULONG)window,
            ASLFR_TitleText, (ULONG)get_string(MSG_SAVE_REPORT),
            ASLFR_InitialDrawer, (ULONG)"RAM:",
            ASLFR_InitialFile, (ULONG)"xsysinfo.txt",
            ASLFR_DoSaveMode, TRUE,
            TAG_DONE)) {
        copy_string(path, (const char *)req->fr_Drawer, sizeof(path));
        if (AddPart((STRPTR)path, req->fr_File, sizeof(path))) {
            set_sleep(TRUE);
            saved = export_to_file(path);
            set_sleep(FALSE);

            if (saved)
                snprintf(message, sizeof(message),
                         get_string(MSG_REPORT_SAVED), path);
            else
                copy_string(message, get_string(MSG_REPORT_SAVE_FAILED),
                            sizeof(message));
            set_text(status_text, message);
        }
    }

    MUI_FreeAslRequest(req);
}

static void show_about(void)
{
    char gadgets[32];

    snprintf(gadgets, sizeof(gadgets), "*%s", get_string(MSG_BTN_OK));
    /* APTR: the string parameter types differ between MUI SDKs. */
    MUI_Request(mui_app, main_window, 0, (APTR)XSYSINFO_NAME,
                (APTR)gadgets,
                (APTR)"\33c\33b%s %s\33n\n%s\n\n"
                      "(C) 2025-2026 Stefan Reinauer\n"
                      "https://github.com/reinauer/xsysinfo",
                (ULONG)XSYSINFO_NAME, (ULONG)XSYSINFO_VERSION,
                (ULONG)get_string(MSG_TAGLINE));
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void dispose_interface(void)
{
    if (mui_app) {
        MUI_DisposeObject(mui_app);
        mui_app = NULL;
    }
    if (you_gauge_class) {
        MUI_DeleteCustomClass(you_gauge_class);
        you_gauge_class = NULL;
    }
    CloseLibrary(MUIMasterBase);
    MUIMasterBase = NULL;
}

BOOL mui_gui_run(const char *version_string, const char *startup_warning)
{
    ULONG signals = 0;
    ULONG clock_signal;
    BOOL running = TRUE;
    ULONG is_open = FALSE;     /* get() stores a full ULONG */
    LONG active;

    MUIMasterBase = OpenLibrary((CONST_STRPTR)MUIMASTER_NAME, MUI_MIN_VERSION);
    if (!MUIMasterBase) {
        debug(XSYSINFO_NAME ": muimaster.library V%d not available\n",
              MUI_MIN_VERSION);
        return FALSE;
    }

    if (!create_application(version_string)) {
        debug(XSYSINFO_NAME ": Could not create MUI application\n");
        dispose_interface();
        return FALSE;
    }

    fill_lists();
    update_all_values();

    set(main_window, MUIA_Window_Open, TRUE);
    get(main_window, MUIA_Window_Open, &is_open);
    if (!is_open) {
        debug(XSYSINFO_NAME ": Could not open MUI window\n");
        dispose_interface();
        return FALSE;
    }

    {
        struct Window *window = NULL;
        get(main_window, MUIA_Window_Window, &window);
        display_ready(window ? window->WScreen : NULL);
    }
    if (startup_warning)
        set_text(status_text, startup_warning);

    while (running) {
        ULONG id = DoMethod(mui_app, MUIM_Application_NewInput,
                            (ULONG)&signals);

        switch (id) {
            case (ULONG)MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;

            case ID_ABOUT:
                show_about();
                break;

            case ID_ABOUT_MUI:
                DoMethod(mui_app, MUIM_Application_AboutMUI,
                         (ULONG)main_window);
                break;

            case ID_MUI_PREFS:
                DoMethod(mui_app, MUIM_Application_OpenConfigWindow, 0);
                break;

            case ID_BENCHMARK:
                run_speed_test();
                break;

            case ID_SAVE:
                save_report();
                break;

            case ID_SOFTWARE_PAGE:
                active = 0;
                get(software_cycle, MUIA_Cycle_Active, &active);
                show_software_list(active);
                break;

            case ID_SCALE:
                active = 0;
                get(scale_cycle, MUIA_Cycle_Active, &active);
                app->bar_scale = active ? SCALE_SHRINK : SCALE_EXPAND;
                update_speed_values();
                break;

            case ID_MEMORY_SELECT:
                update_memory_details();
                break;

            case ID_MEMORY_SPEED:
                measure_selected_memory();
                break;

            case ID_DRIVE_SELECT:
                select_drive();
                break;

            case ID_DRIVE_SPEED:
                measure_selected_drive();
                break;

            case ID_DRIVE_SCSI:
                show_scsi_devices();
                break;

            case ID_SCSI_CLOSE:
                set(scsi_window, MUIA_Window_Open, FALSE);
                break;

            case ID_BOARD_SELECT:
                update_board_details();
                break;

            case ID_BOARD_DETAILS:
                update_board_details();
                if (app->selected_board >= 0)
                    set(board_details_window, MUIA_Window_Open, TRUE);
                break;

            case ID_BOARD_DETAILS_CLOSE:
                set(board_details_window, MUIA_Window_Open, FALSE);
                break;

            case ID_BOARD_DISPLAY:
                active = 0;
                get(board_cycle, MUIA_Cycle_Active, &active);
                app->board_display = (BoardDisplay)active;
                DoMethod(board_list_obj, MUIM_List_Redraw,
                         MUIV_List_Redraw_All);
                update_board_details();
                break;

            default:
                if (id > ID_CACHE_BASE && id < ID_CACHE_BASE + CACHE_SETTING_COUNT) {
                    toggle_cache_setting(id - ID_CACHE_BASE);
                    refresh_cache_status();
                    update_hardware_values();
                }
                break;
        }

        if (running && signals) {
            /* Like the classic loop: tick the RTC only while it is shown. */
            active = -1;
            get(register_group, MUIA_Group_ActivePage, &active);
            if (active == PAGE_HARDWARE) {
                get(hardware_register, MUIA_Group_ActivePage, &active);
                clock_signal = clock_refresh_signal(active == HARDWARE_CLOCK);
            } else {
                clock_signal = clock_refresh_signal(FALSE);
            }

            signals = Wait(signals | clock_signal | SIGBREAKF_CTRL_C);
            if (signals & SIGBREAKF_CTRL_C)
                running = FALSE;
            if ((signals & clock_signal) && clock_refresh_ready())
                update_clock_time();
        }
    }

    cleanup_clock_refresh();
    dispose_interface();

    return TRUE;
}
