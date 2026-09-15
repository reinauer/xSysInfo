// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Expansion boards enumeration and view
 */

#include <string.h>
#include <stdio.h>

#include <libraries/configvars.h>
#include <libraries/identify.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <proto/graphics.h>
#include <proto/identify.h>

#include "xsysinfo.h"
#include "boards.h"
#include "gui.h"
#include "locale_str.h"
#include "debug.h"
#ifdef __KICK13__
#include "tagitem.h"
#endif

#define BOARD_LIST_FIRST_Y 56
#define BOARD_LIST_LINE_H 10
#define BOARD_LIST_BOTTOM_PAD 50

#define OPENPCI_MANUFACTURER_FIRST 1729
#define OPENPCI_MANUFACTURER_LAST  1735
#define MIN_IDENTIFY_PCI_VERSION   45

/*
 * Minimal OpenPCI pci_dev prefix used by identify.library when traversing
 * PCI boards via IdPciExpansionTags(IDTAG_Expansion, ...).
 */
struct pci_dev {
    void *bus;
    struct pci_dev *next;
    struct pci_dev *pred;
    UBYTE devfn;
    UBYTE kludgefill;
    UWORD vendor;
    UWORD device;
    ULONG devclass;
};

/* Global board list */
BoardList board_list;

#define BOARD_SERIAL_HEX_THRESHOLD 100000UL

/* External references */
extern AppContext *app;
extern struct Library *IdentifyBase;

/*
 * Format board size to human-readable string
 */
void format_board_size(ULONG size, char *buffer, ULONG bufsize)
{
    if (size >= 1024 * 1024) {
        snprintf(buffer, bufsize, "%luM", (unsigned long)(size / (1024 * 1024)));
    } else if (size >= 1024) {
        snprintf(buffer, bufsize, "%luK", (unsigned long)(size / 1024));
    } else {
        snprintf(buffer, bufsize, "%lu", (unsigned long)size);
    }
}

static void format_board_serial(ULONG serial, char *buffer, ULONG bufsize)
{
    if (serial >= BOARD_SERIAL_HEX_THRESHOLD) {
        snprintf(buffer, bufsize, "$%08lX", (unsigned long)serial);
    } else {
        snprintf(buffer, bufsize, "%lu", (unsigned long)serial);
    }
}

/*
 * Get board type string
 */
const char *get_board_type_string(BoardType type)
{
    switch (type) {
        case BOARD_ZORRO_II:
            return get_string(MSG_ZORRO_II);
        case BOARD_ZORRO_III:
            return get_string(MSG_ZORRO_III);
        case BOARD_PCI:
            return "PCI";
        default:
            return get_string(MSG_UNKNOWN);
    }
}

static BOOL append_zorro_board(struct ConfigDev *cd, const char *manufacturer,
                               const char *product)
{
    BoardInfo *board;

    if (board_list.count >= MAX_BOARDS)
        return FALSE;

    board = &board_list.boards[board_list.count];

    board->board_address = (ULONG)cd->cd_BoardAddr;
    board->board_size = cd->cd_BoardSize;
    board->manufacturer_id = cd->cd_Rom.er_Manufacturer;
    board->product_id = cd->cd_Rom.er_Product;
    board->serial_number = (ULONG)cd->cd_Rom.er_SerialNumber;

    debug("  boards: Found Zorro board at $%08X\n",
          (ULONG)board->board_address);

    if ((cd->cd_Rom.er_Flags & ERFF_ZORRO_III) ||
        ((cd->cd_Rom.er_Type & ERT_TYPEMASK) == ERT_ZORROIII)) {
        board->board_type = BOARD_ZORRO_III;
    } else {
        board->board_type = BOARD_ZORRO_II;
    }

    format_board_size(board->board_size, board->size_string,
                      sizeof(board->size_string));
    snprintf(board->address_string, sizeof(board->address_string),
             "$%08lX", (unsigned long)board->board_address);
    format_board_serial(board->serial_number, board->detail_string,
                        sizeof(board->detail_string));

    if (manufacturer && manufacturer[0]) {
        snprintf(board->manufacturer_name, sizeof(board->manufacturer_name),
                 "%s", manufacturer);
    } else {
        snprintf(board->manufacturer_name, sizeof(board->manufacturer_name),
                 "ID %u", board->manufacturer_id);
    }

    if (product && product[0]) {
        snprintf(board->product_name, sizeof(board->product_name), "%s",
                 product);
    } else {
        snprintf(board->product_name, sizeof(board->product_name),
                 "Product %u", board->product_id);
    }

    board_list.count++;
    return TRUE;
}

static BOOL is_openpci_resource(const struct ConfigDev *cd)
{
    UWORD manufacturer = cd->cd_Rom.er_Manufacturer;

    return manufacturer >= OPENPCI_MANUFACTURER_FIRST &&
           manufacturer <= OPENPCI_MANUFACTURER_LAST;
}

static BOOL append_pci_board(struct pci_dev *pci, const char *manufacturer,
                             const char *product, const char *class_name)
{
    BoardInfo *board;

    if (board_list.count >= MAX_BOARDS)
        return FALSE;

    if (!pci)
        return TRUE;

    board = &board_list.boards[board_list.count];
    board->board_type = BOARD_PCI;
    board->manufacturer_id = pci->vendor;
    board->product_id = pci->device;
    board->serial_number = 0;
    board->board_address = 0;
    board->board_size = 0;

    snprintf(board->address_string, sizeof(board->address_string), "--");
    snprintf(board->size_string, sizeof(board->size_string), "--");
    if (class_name && class_name[0]) {
        copy_string(board->detail_string, class_name,
                    sizeof(board->detail_string));
    } else {
        snprintf(board->detail_string, sizeof(board->detail_string), "PCI");
    }

    if (manufacturer && manufacturer[0]) {
        snprintf(board->manufacturer_name, sizeof(board->manufacturer_name),
                 "%s", manufacturer);
    } else {
        snprintf(board->manufacturer_name, sizeof(board->manufacturer_name),
                 "$%04lx", (unsigned long)pci->vendor);
    }

    if (product && product[0]) {
        snprintf(board->product_name, sizeof(board->product_name), "%s",
                 product);
    } else {
        snprintf(board->product_name, sizeof(board->product_name),
                 "$%04lx", (unsigned long)pci->device);
    }

    debug("  boards: Found PCI board %04lx:%04lx\n",
          (unsigned long)pci->vendor, (unsigned long)pci->device);

    board_list.count++;
    return TRUE;
}

static void enumerate_pci_boards_with_identify(void)
{
    struct pci_dev *pci = NULL;
    LONG result;
    char manufacturer[64];
    char product[64];
    char class_name[64];

    if (!IdentifyBase || IdentifyBase->lib_Version < MIN_IDENTIFY_PCI_VERSION) {
        debug("  boards: Skipping PCI scan, identify.library v%u lacks "
              "IdPciExpansion\n", IdentifyBase ? IdentifyBase->lib_Version : 0);
        return;
    }

    debug("  boards: Scanning PCI boards via identify.library...\n");

    while (board_list.count < MAX_BOARDS) {
        manufacturer[0] = '\0';
        product[0] = '\0';
        class_name[0] = '\0';

        result = IdPciExpansionTags(
            IDTAG_Expansion, (ULONG)&pci,
            IDTAG_ManufStr, (ULONG)manufacturer,
            IDTAG_ProdStr, (ULONG)product,
            IDTAG_ClassStr, (ULONG)class_name,
            IDTAG_StrLength, sizeof(product),
            TAG_DONE);

        if (result != IDERR_OKAY) {
            debug("  boards: PCI scan stopped with result %ld\n",
                  (long)result);
            break;
        }

        if (!append_pci_board(pci, manufacturer, product, class_name))
            break;
    }
}

static void enumerate_zorro_boards_with_identify(void)
{
    struct ConfigDev *cd = NULL;
    LONG result;
    ULONG class_id;
    UBYTE unknown;
    char manufacturer[64];
    char product[64];
    char class_name[64];

    debug("  boards: Scanning physical expansion boards via identify.library...\n");

    while (board_list.count < MAX_BOARDS) {
        class_id = IDCID_UNKNOWN;
        unknown = FALSE;
        manufacturer[0] = '\0';
        product[0] = '\0';
        class_name[0] = '\0';

        result = IdExpansionTags(
            IDTAG_Expansion, (ULONG)&cd,
            IDTAG_ManufStr, (ULONG)manufacturer,
            IDTAG_ProdStr, (ULONG)product,
            IDTAG_ClassStr, (ULONG)class_name,
            IDTAG_ClassID, (ULONG)&class_id,
            IDTAG_UnknownFlag, (ULONG)&unknown,
            IDTAG_StrLength, sizeof(product),
            TAG_DONE);

        if (result != IDERR_OKAY)
            break;

        if (class_id == IDCID_VIRTUAL) {
            if (is_openpci_resource(cd)) {
                debug("  boards: Skipping OpenPCI virtual expansion "
                      "%u/%u\n",
                      cd->cd_Rom.er_Manufacturer,
                      cd->cd_Rom.er_Product);
            } else {
                debug("  boards: Skipping virtual expansion board "
                      "%u/%u\n",
                      cd->cd_Rom.er_Manufacturer,
                      cd->cd_Rom.er_Product);
            }
            continue;
        }

        append_zorro_board(cd, manufacturer, product);
    }

    enumerate_pci_boards_with_identify();
}

static void enumerate_zorro_boards_raw(void)
{
    struct ConfigDev *cd = NULL;
    struct Library *ExpansionBase;

    debug("  boards: Opening expansion.library...\n");
    ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library",
                                MIN_EXPANSION_VERSION);
    if (!ExpansionBase) {
        Printf((CONST_STRPTR)"Could not open expansion.library v%d\n",
               MIN_EXPANSION_VERSION);
        return;
    }

    debug("  boards: Scanning raw ConfigDevs...\n");
    while ((cd = (struct ConfigDev *)FindConfigDev(cd, -1, -1)) != NULL) {
        if (!append_zorro_board(cd, NULL, NULL))
            break;
    }

    debug("  boards: Closing expansion.library...\n");
    CloseLibrary(ExpansionBase);
}

/*
 * Enumerate all expansion boards
 */
void enumerate_boards(void)
{
    debug("  boards: Starting enumeration...\n");

    memset(&board_list, 0, sizeof(board_list));

    if (IdentifyBase) {
        enumerate_zorro_boards_with_identify();
    } else {
        enumerate_zorro_boards_raw();
    }

    debug("  boards: Enumeration complete, found %d boards\n", (LONG)board_list.count);
}

static void draw_board_field_clipped(WORD x, WORD y, const char *text,
                                     WORD max_x)
{
    draw_text_clipped(x, y, text, max_x - x);
}

static LONG visible_board_rows(void)
{
    LONG rows;

    rows = ((LONG)app->screen_height - BOARD_LIST_BOTTOM_PAD -
            BOARD_LIST_FIRST_Y + BOARD_LIST_LINE_H - 1) / BOARD_LIST_LINE_H;
    return rows > 0 ? rows : 1;
}

static LONG max_board_scroll(void)
{
    LONG rows = visible_board_rows();

    if ((LONG)board_list.count <= rows) {
        return 0;
    }

    return (LONG)board_list.count - rows;
}

/*
 * Draw boards view
 */
void draw_boards_view(void)
{
    struct RastPort *rp = app->rp;
    WORD y;
    ULONG i;
    char buffer[128];
    Button *btn;

    /* Draw title panel */
    draw_panel(20,  0, 600, 24, NULL);

    draw_text_centered(20, 14, 600, get_string(MSG_BOARDS_INFO), COLOR_TEXT);

    /* Draw column headers */
    y = 40;
    SetAPen(rp, COLOR_TEXT);

    TightText(rp,  25, y, (CONST_STRPTR)get_string(MSG_BOARD_ADDRESS), -1, 4);
    TightText(rp, 136, y, (CONST_STRPTR)get_string(MSG_BOARD_SIZE), -1, 4);
    TightText(rp, 214, y, (CONST_STRPTR)get_string(MSG_BOARD_TYPE), -1, 4);
    TightText(rp, 296, y, (CONST_STRPTR)get_string(MSG_PRODUCT), -1, 4);
    TightText(rp, 420, y, (CONST_STRPTR)get_string(MSG_MANUFACTURER), -1, 4);
    TightText(rp, 550, y, (CONST_STRPTR)get_string(MSG_SERIAL_NO), -1, 4);

    /* Draw separator line */
    SetAPen(rp, COLOR_BUTTON_DARK);
    Move(rp, 20, y + 4);
    Draw(rp, 628, y + 4);

    /* Draw board entries */
    if (app->board_scroll > max_board_scroll()) {
        app->board_scroll = max_board_scroll();
    }

    y = BOARD_LIST_FIRST_Y;
    for (i = app->board_scroll;
         i < board_list.count && y < app->screen_height - BOARD_LIST_BOTTOM_PAD;
         i++) {

        BoardInfo *board = &board_list.boards[i];

        SetAPen(rp, COLOR_HIGHLIGHT);
        SetBPen(rp, COLOR_BACKGROUND);

        /* Address */
        draw_board_field_clipped(25, y, board->address_string, 136);

        /* Size */
        draw_board_field_clipped(136, y, board->size_string, 214);

        /* Type */
        draw_board_field_clipped(214, y,
                                 get_board_type_string(board->board_type),
                                 296);

        /* Product */
        if (app->board_display == BOARD_DISPLAY_NAMES) {
            snprintf(buffer, sizeof(buffer), "%s", board->product_name);
        } else if (app->board_display == BOARD_DISPLAY_HEX) {
            snprintf(buffer, sizeof(buffer),
                     board->board_type == BOARD_PCI ? "$%04lX" : "$%02lX",
                     (unsigned long)board->product_id);
        } else {
            snprintf(buffer, sizeof(buffer), "%u", board->product_id);
        }
        draw_board_field_clipped(296, y, buffer, 420);

        /* Manufacturer */
        if (app->board_display == BOARD_DISPLAY_NAMES) {
            snprintf(buffer, sizeof(buffer), "%s", board->manufacturer_name);
        } else if (app->board_display == BOARD_DISPLAY_HEX) {
            snprintf(buffer, sizeof(buffer), "$%04lX",
                     (unsigned long)board->manufacturer_id);
        } else {
            snprintf(buffer, sizeof(buffer), "%u", board->manufacturer_id);
        }
        draw_board_field_clipped(420, y, buffer, 550);

        /* Serial or PCI class */
        if (board->board_type == BOARD_PCI ||
            app->board_display == BOARD_DISPLAY_NAMES) {
            snprintf(buffer, sizeof(buffer), "%s", board->detail_string);
        } else {
            snprintf(buffer, sizeof(buffer),
                     app->board_display == BOARD_DISPLAY_HEX ? "$%08lX" : "%lu",
                     (unsigned long)board->serial_number);
        }
        draw_board_field_clipped(550, y, buffer, SCREEN_WIDTH - 4);

        y += BOARD_LIST_LINE_H;
    }

    if (board_list.count == 0) {
        SetAPen(rp, COLOR_TEXT);
        SetBPen(rp, COLOR_BACKGROUND);
        draw_text_clipped(200, 120, get_string(MSG_BOARDS_NO_BOARDS_FOUND),
                          SCREEN_WIDTH - 204);
    }

    /* Draw bottom buttons */
    btn = find_button(BTN_BOARD_PREV);
    if (btn) draw_button(btn);
    btn = find_button(BTN_BOARD_NEXT);
    if (btn) draw_button(btn);
    btn = find_button(BTN_BOARD_DISPLAY);
    if (btn) draw_cycle_button(btn);
    btn = find_button(BTN_BOARD_EXIT);
    if (btn) draw_button(btn);
}

/*
 * Update buttons for Boards view
 */
void boards_view_update_buttons(void)
{
    static const LocaleStringID display_labels[BOARD_DISPLAY_COUNT] = {
        MSG_BOARD_NAMES, MSG_BOARD_DECIMAL, MSG_BOARD_HEX
    };
    LONG max_scroll = max_board_scroll();

    add_button(170, 188, 100, 12,
               get_string(display_labels[app->board_display]),
               BTN_BOARD_DISPLAY, board_list.count > 0);

    if (max_scroll > 0) {
        add_button(20, 188, 60, 12,
                   get_string(MSG_BTN_PREV), BTN_BOARD_PREV,
                   app->board_scroll > 0);
        add_button(90, 188, 60, 12,
                   get_string(MSG_BTN_NEXT), BTN_BOARD_NEXT,
                   app->board_scroll < max_scroll);
        add_button(560, 188, 60, 12,
                   get_string(MSG_BTN_EXIT), BTN_BOARD_EXIT, TRUE);
        return;
    }

    add_button(20, 188, 60, 12,
               get_string(MSG_BTN_EXIT), BTN_BOARD_EXIT, TRUE);
}

/*
 * Handle button press for Boards view
 */
void boards_view_handle_button(ButtonID id)
{
    switch (id) {
        case BTN_BOARD_PREV:
            if (app->board_scroll > 0) {
                app->board_scroll--;
                redraw_current_view();
            }
            break;

        case BTN_BOARD_NEXT:
            if (app->board_scroll < max_board_scroll()) {
                app->board_scroll++;
                redraw_current_view();
            }
            break;

        case BTN_BOARD_DISPLAY:
            app->board_display = (app->board_display + 1) % BOARD_DISPLAY_COUNT;
            redraw_current_view();
            break;

        case BTN_BOARD_EXIT:
            switch_to_view(VIEW_MAIN);
            break;

        default:
            break;
    }
}
