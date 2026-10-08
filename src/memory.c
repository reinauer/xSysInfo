// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Memory information and view
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <exec/execbase.h>
#include <exec/memory.h>

#include <proto/exec.h>
#include <proto/graphics.h>

#include "xsysinfo.h"
#include "format.h"
#include "memory.h"
#include "growlist.h"
#include "gui.h"
#include "locale_str.h"
#include "benchmark.h"
#include "debug.h"
#include "hardware.h"

/* Global memory region list */
MemoryRegionList memory_regions;

#define MEMORY_INFO_X        128
#define MEMORY_VALUE_OFFSET  168
#define MEMORY_VALUE_X       (MEMORY_INFO_X + MEMORY_VALUE_OFFSET)
#define MEMORY_VALUE_MAX_X   618

/* External references */
extern struct ExecBase *SysBase;
extern HardwareInfo hw_info;
extern AppContext *app;

/*
 * Get human-readable memory type string
 */
const char *get_memory_type_string(UWORD attrs, APTR addr)
{
    static char buffer[64];
    size_t pos;
    ULONG address = (ULONG)addr;

    //strcat crashes on a 68000/010?!?!
    /* Check for specific memory regions */
    if (attrs & MEMF_CHIP) {
        pos = snprintf(buffer, sizeof(buffer), "Chip RAM");
    } else if (address >= 0xC00000 && address < 0xD80000 &&
               hw_info.gary_type != GARY_A1000) {
        /* Ranger/Slow RAM area. Skip on A1000: there is no motherboard
         * Ranger option, so memory in this window must be coming over
         * the CPU expansion (e.g. Spirit Inboard 1000) and is actually
         * CPU-side fast RAM. */
        pos = snprintf(buffer, sizeof(buffer), "Slow RAM");
    } else if (attrs & MEMF_FAST) {
        /* The DMA24 flag below already implies a 24-bit address; avoid
         * two different 24-bit hints in one line (issue #26) */
        if (attrs & MEMF_24BITDMA) {
            pos = snprintf(buffer, sizeof(buffer), "Fast RAM");
        } else if (address < 0x01000000) {
            pos = snprintf(buffer, sizeof(buffer), "Fast RAM (24-bit)");
        } else {
            pos = snprintf(buffer, sizeof(buffer), "Fast RAM (32-bit)");
        }
    } else {
        pos = snprintf(buffer, sizeof(buffer), "RAM");
    }

    if (attrs & MEMF_LOCAL) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", Local");
    }

    if (attrs & MEMF_PUBLIC) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", Public");
    }
    if (attrs & MEMF_KICK) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", Kick");
    }
    if (attrs & MEMF_24BITDMA) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", DMA24");
    }

    return buffer;
}

/*
 * Analyze memory region - count chunks and find largest block
 */
void analyze_memory_region(struct MemHeader *mh, ULONG *chunks, ULONG *largest)
{
    struct MemChunk *mc;
    ULONG count = 0;
    ULONG max_size = 0;

    *chunks = 0;
    *largest = 0;

    if (!mh) return;

    /* Walk the free list */
    for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next) {
        count++;
        if (mc->mc_Bytes > max_size) {
            max_size = mc->mc_Bytes;
        }
    }

    *chunks = count;
    *largest = max_size;
}

/*
 * Find the live Exec memory header for a stored region.
 */
static struct MemHeader *find_region_header(MemoryRegion *region)
{
    struct MemHeader *mh;

    if (!region) return NULL;

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        if (mh == region->memListNode ||
            (mh->mh_Lower == region->lower_bound &&
             mh->mh_Upper == region->upper_bound)) {
            return mh;
        }
    }

    return NULL;
}

/*
 * Find the largest currently free chunk in a memory header.
 */
static APTR find_largest_free_chunk(struct MemHeader *mh, ULONG *size)
{
    struct MemChunk *mc;
    APTR chunk = NULL;
    ULONG largest = 0;

    if (size) *size = 0;
    if (!mh) return NULL;

    for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next) {
        if (mc->mc_Bytes > largest) {
            largest = mc->mc_Bytes;
            chunk = (APTR)mc;
        }
    }

    if (size) *size = largest;
    return chunk;
}

void free_memory_regions(void)
{
    free_list((APTR *)&memory_regions.regions, &memory_regions.capacity,
              sizeof(MemoryRegion));
    memory_regions.count = 0;
}

/*
 * Enumerate all memory regions
 */
void enumerate_memory_regions(void)
{
    struct MemHeader *mh;
    ULONG headers = 0;

    free_memory_regions();
    memory_regions.total_chip_size = 0;
    memory_regions.total_fast_size = 0;

    /* Allocate before the scan, so it reads free counts that already
     * include this list. */
    Forbid();
    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
        headers++;
    Permit();
    if (headers > MAX_MEMORY_REGIONS)
        headers = MAX_MEMORY_REGIONS;
    grow_list((APTR *)&memory_regions.regions, &memory_regions.capacity,
              0, headers, sizeof(MemoryRegion), MAX_MEMORY_REGIONS);

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {

        ULONG start = (ULONG)mh->mh_Lower & 0xffff8000;
        ULONG size = (ULONG)mh->mh_Upper - start;

        /* Use the same region capacities as the memory view. Exec's
         * MEMF_TOTAL excludes reserved bytes at the start of each region. */
        if (mh->mh_Attributes & MEMF_CHIP)
            memory_regions.total_chip_size += size;
        else if (mh->mh_Attributes & MEMF_FAST)
            memory_regions.total_fast_size += size;

        if (memory_regions.count >= memory_regions.capacity) continue;

        MemoryRegion *region = &memory_regions.regions[memory_regions.count];

        region->start_address = (APTR)start;
        region->end_address = mh->mh_Upper - 1;
        region->total_size = size;
        region->mem_type = mh->mh_Attributes;
        region->priority = mh->mh_Node.ln_Pri;
        region->lower_bound = mh->mh_Lower;
        region->upper_bound = mh->mh_Upper;
        region->first_free = mh->mh_First;
        region->amount_free = mh->mh_Free;
        region->memListNode = mh; //to find me in the list

        /* Detect 2MB Chip RAM variant of ECS Agnus */
        if (region->mem_type & MEMF_CHIP) {
            if (hw_info.agnus_type == AGNUS_ECS_PAL && mh->mh_Upper > (APTR)0x100000) {
                hw_info.agnus_type = AGNUS_ECS_2MB_PAL;
            }
            if (hw_info.agnus_type == AGNUS_ECS_NTSC && mh->mh_Upper > (APTR)0x100000) {
                hw_info.agnus_type = AGNUS_ECS_2MB_NTSC;
            }
        }

        analyze_memory_region(mh, &region->num_chunks, &region->largest_block);

        if (mh->mh_Node.ln_Name) {
            strncpy(region->node_name, mh->mh_Node.ln_Name,
                    sizeof(region->node_name) - 1);
        } else {
            strncpy(region->node_name, "(unnamed)",
                    sizeof(region->node_name) - 1);
        }

        strncpy(region->type_string,
                get_memory_type_string(mh->mh_Attributes, mh->mh_Lower),
                sizeof(region->type_string) - 1);

        memory_regions.count++;
    }

    Permit();
}

/*
 * Refresh a single memory region (for updated free memory info)
 */
void refresh_memory_region(ULONG index)
{
    struct MemHeader *mh;

    if (index >= memory_regions.count) return;

    Forbid();

    mh = find_region_header(&memory_regions.regions[index]);
    if (mh) {
        MemoryRegion *region = &memory_regions.regions[index];
        region->memListNode = mh;
        region->first_free = mh->mh_First;
        region->amount_free = mh->mh_Free;
        analyze_memory_region(mh, &region->num_chunks, &region->largest_block);
    }

    Permit();
}

/*
 * Measure memory read speed for a region
 * Returns bytes per second
 */
ULONG measure_memory_speed(ULONG index)
{
    MemoryRegion *region;
    struct MemHeader *mh;
    APTR chunk = NULL;
    APTR buffer = NULL;
    ULONG chunk_size = 0;
    ULONG buffer_size;
    ULONG bytes_per_sec = 0;

    if (index >= memory_regions.count) return 0;

    region = &memory_regions.regions[index];

    /*
     * Allocate from the selected region without rewriting Exec's global
     * memory list.  AllocAbs() will fail if this exact free chunk is no
     * longer available.
     */
    Forbid();

    mh = find_region_header(region);
    if (mh) {
        region->memListNode = mh;
        region->first_free = mh->mh_First;
        region->amount_free = mh->mh_Free;
        analyze_memory_region(mh, &region->num_chunks, &region->largest_block);
        chunk = find_largest_free_chunk(mh, &chunk_size);
    }

    buffer_size = 64 * 1024;
    if (buffer_size > chunk_size / 2) {
        buffer_size = chunk_size / 2;
    }

    /* Ensure reasonable minimum size */
    if (buffer_size < 256) {
        region->speed_measured = TRUE;
        region->speed_bytes_sec = 0;
        Permit();
        return 0;
    }

    buffer = AllocAbs(buffer_size, chunk);
    Permit();

    if (buffer) {
        bytes_per_sec = measure_mem_read_speed((volatile ULONG *)buffer,
                                               buffer_size, 16);
        FreeMem(buffer, buffer_size);
    }

    region->speed_bytes_sec = bytes_per_sec;
    region->speed_measured = TRUE;
    return bytes_per_sec;
}

/*
 * Draw memory view
 */
void format_memory_speed(const MemoryRegion *region, char *buffer,
                                size_t size)
{
    if (region->speed_measured && region->speed_bytes_sec > 0) {
        format_transfer_rate(region->speed_bytes_sec, TRUE, buffer, size);
    } else {
        snprintf(buffer, size, "---");
    }
}

static void draw_memory_value(WORD y, const char *value);

static void clear_memory_values(WORD first, WORD last)
{
    SetAPen(app->rp, COLOR_PANEL_BG);
    RectFill(app->rp, MEMORY_VALUE_X, first - 8, MEMORY_VALUE_MAX_X, last + 2);
}


static void draw_memory_speed_row(void)
{
    char buffer[64];
    MemoryRegion *region;

    if (app->memory_region_index < 0 ||
        app->memory_region_index >= (LONG)memory_regions.count) {
        return;
    }

    region = &memory_regions.regions[app->memory_region_index];
    format_memory_speed(region, buffer, sizeof(buffer));

    clear_memory_values(164, 164);
    draw_memory_value(164, buffer);
}

static void draw_memory_value(WORD y, const char *value)
{
    struct RastPort *rp = app->rp;

    SetAPen(rp, COLOR_HIGHLIGHT);
    SetBPen(rp, COLOR_PANEL_BG);
    draw_text_clipped(MEMORY_VALUE_X, y, value,
                      MEMORY_VALUE_MAX_X - MEMORY_VALUE_X);
}

static void draw_memory_values(void)
{
    char buffer[64];
    WORD y = 44;
    MemoryRegion *region;

    refresh_memory_region(app->memory_region_index);
    region = &memory_regions.regions[app->memory_region_index];

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->start_address);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->end_address);
    draw_memory_value(y, buffer);
    y += 10;

    format_size(region->total_size, buffer, sizeof(buffer));
    draw_memory_value(y, buffer);
    y += 10;

    draw_memory_value(y, region->type_string);
    y += 10;

    snprintf(buffer, sizeof(buffer), "%d", region->priority);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->lower_bound);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->upper_bound);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "$%08lX",
             (unsigned long)region->first_free);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "%lu Bytes",
             (unsigned long)region->amount_free);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "%lu Bytes",
             (unsigned long)region->largest_block);
    draw_memory_value(y, buffer);
    y += 10;

    snprintf(buffer, sizeof(buffer), "%lu",
             (unsigned long)region->num_chunks);
    draw_memory_value(y, buffer);
    y += 10;

    draw_memory_value(y, region->node_name);
    y += 10;

    format_memory_speed(region, buffer, sizeof(buffer));
    draw_memory_value(y, buffer);
}

static void draw_memory_buttons(void)
{
    Button *btn;

    btn = find_button(BTN_MEM_PREV);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_COUNTER);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_NEXT);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_SPEED);
    if (btn) draw_button(btn);
    btn = find_button(BTN_MEM_EXIT);
    if (btn) draw_button(btn);
}

/*
 * Draw memory data area (info panel and navigation buttons - no title)
 */
static void draw_memory_data(BOOL full_redraw)
{
    static const LocaleStringID labels[] = {
        MSG_START_ADDRESS,
        MSG_END_ADDRESS,
        MSG_TOTAL_SIZE,
        MSG_MEMORY_TYPE,
        MSG_PRIORITY,
        MSG_LOWER_BOUND,
        MSG_UPPER_BOUND,
        MSG_FIRST_ADDRESS,
        MSG_AMOUNT_FREE,
        MSG_LARGEST_BLOCK,
        MSG_NUM_CHUNKS,
        MSG_NODE_NAME,
        MSG_MEMORY_SPEED
    };
    ULONG row;

    if (memory_regions.count == 0) {
        draw_text(200, 120, "No memory regions found", COLOR_TEXT);
        return;
    }

    if (full_redraw) {
        draw_panel(100, 28, 520, 150, NULL);
        for (row = 0; row < sizeof(labels) / sizeof(labels[0]); row++) {
            draw_label_value(128, 44 + row * 10, get_string(labels[row]),
                             NULL, 168);
        }
    } else {
        clear_memory_values(44, 164);
    }
    draw_memory_values();
    draw_memory_buttons();
}

void draw_memory_view(void)
{
    /* Draw title panel */
    draw_panel(100, 0, 520, 24, NULL);

    draw_text_centered(100, 14, 520, get_string(MSG_MEMORY_INFO), COLOR_TEXT);

    /* Draw data area with full panel borders */
    draw_memory_data(TRUE);
}

/*
 * Update buttons for Memory view
 */
void memory_view_update_buttons(void)
{
    static char counter_str[16];
    snprintf(counter_str, sizeof(counter_str), "%" PRId32 " / %lu",
             app->memory_region_index + 1, (unsigned long)memory_regions.count);
    add_button(100, 188, 52, 12,
               get_string(MSG_BTN_PREV), BTN_MEM_PREV,
               app->memory_region_index > 0);
    add_button(160, 188, 52, 12,
               counter_str, BTN_MEM_COUNTER, FALSE);
    add_button(220, 188, 52, 12,
               get_string(MSG_BTN_NEXT), BTN_MEM_NEXT,
               app->memory_region_index < (LONG)memory_regions.count - 1);
    add_button(280, 188, 52, 12,
               get_string(MSG_BTN_SPEED), BTN_MEM_SPEED, TRUE);
    add_button(340, 188, 52, 12,
               get_string(MSG_BTN_EXIT), BTN_MEM_EXIT, TRUE);
}

/*
 * Handle button press for Memory view
 */
void memory_view_handle_button(ButtonID id)
{
    switch (id) {
        case BTN_MEM_PREV:
            if (app->memory_region_index > 0) {
                app->memory_region_index--;
                /* Only redraw data area, not the entire screen */
                update_button_states();
                draw_memory_data(FALSE);
            }
            break;

        case BTN_MEM_NEXT:
            if (app->memory_region_index < (LONG)memory_regions.count - 1) {
                app->memory_region_index++;
                /* Only redraw data area, not the entire screen */
                update_button_states();
                draw_memory_data(FALSE);
            }
            break;

        case BTN_MEM_SPEED:
            if (app->memory_region_index >= 0 &&
                app->memory_region_index < (LONG)memory_regions.count) {
                show_status_overlay(get_string(MSG_MEASURING_SPEED));
                Forbid();
                measure_memory_speed(app->memory_region_index);
                Permit();
                hide_status_overlay();
                draw_memory_speed_row();
            }
            break;

        case BTN_MEM_EXIT:
            switch_to_view(VIEW_MAIN);
            break;

        default:
            break;
    }
}
