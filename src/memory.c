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
#include "memory.h"
#include "gui.h"
#include "locale_str.h"
#include "benchmark.h"
#include "debug.h"
#include "hardware.h"

/* Global memory region list */
MemoryRegionList memory_regions;

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
        pos = snprintf(buffer, sizeof(buffer), "CHIP RAM");
    } else if (address >= 0xC00000 && address < 0xD80000) {
        /* Ranger/Slow RAM area */
        pos = snprintf(buffer, sizeof(buffer), "SLOW RAM");
    } else if (attrs & MEMF_FAST) {
        if (address < 0x01000000) {
            pos = snprintf(buffer, sizeof(buffer), "FAST RAM (24bit)");
        } else {
            pos = snprintf(buffer, sizeof(buffer), "FAST RAM (32bit)");
        }
    } else {
        pos = snprintf(buffer, sizeof(buffer), "RAM");
    }

    if (attrs & MEMF_LOCAL) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", LOCAL");
    }

    if (attrs & MEMF_PUBLIC) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", PUBLIC");
    }
    if (attrs & MEMF_KICK) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", KICK");
    }
    if (attrs & MEMF_24BITDMA) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ", 24BitDMA");
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
 * Enumerate all memory regions
 */
void enumerate_memory_regions(void)
{
    struct MemHeader *mh;

    memset(&memory_regions, 0, sizeof(memory_regions));

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {

        if (memory_regions.count >= MAX_MEMORY_REGIONS) break;

        MemoryRegion *region = &memory_regions.regions[memory_regions.count];

        region->start_address = (APTR)((ULONG)mh->mh_Lower & 0xffff8000);
        region->end_address = mh->mh_Upper - 1;
        region->total_size = (ULONG)(mh->mh_Upper - region->start_address);
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
    ULONG i = 0;

    if (index >= memory_regions.count) return;

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         (struct Node *)mh != (struct Node *)&SysBase->MemList.lh_Tail;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {

        if (i == index) {
            MemoryRegion *region = &memory_regions.regions[index];
            region->first_free = mh->mh_First;
            region->amount_free = mh->mh_Free;
            analyze_memory_region(mh, &region->num_chunks, &region->largest_block);
            break;
        }
        i++;
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
    ULONG buffer_size;
    ULONG bytes_per_sec = 0;

    if (index >= memory_regions.count) return 0;

    region = &memory_regions.regions[index];

    /* Use a reasonable buffer size - 64K for test reads */
    buffer_size = 64 * 1024;

    /* Limit to largest available block (halved for safety margin) */
    if (buffer_size > region->largest_block / 2) {
        buffer_size = region->largest_block / 2;
    }

    /* Ensure reasonable minimum size */
    if (buffer_size < 256) {
        region->speed_measured = TRUE;
        region->speed_bytes_sec = 0;
        return 0;
    }

    /*
        Mega magic: I want to allocate a buffer in exactly this region (to avoid any mem corruption)
        So what to do (according to Thomas Richter):

        Forbid()
        Fetch my entry in the System Memlist
        Save old head and tail of memlist
        Save prev/next-pointer of my entry
        set head and tail to my entry
        set prev/next-pointer of my entry to myself
        alloc mem (there is only one entry left!)
        restore all pointers
        Permit()
    */

    struct MemHeader *oldHead, *oldTail, *mh, *oldSucc, *oldPred, *oldTailPred;
    APTR buffer = NULL;

    Forbid();
    oldHead = (struct MemHeader *)SysBase->MemList.lh_Head;
    oldTail = (struct MemHeader *)SysBase->MemList.lh_Tail;
    oldTailPred = (struct MemHeader *)SysBase->MemList.lh_TailPred;
    mh = region->memListNode;
    if (mh) {
        oldSucc = (struct MemHeader *)mh->mh_Node.ln_Succ;
        oldPred = (struct MemHeader *)mh->mh_Node.ln_Pred;
        //now start modifying the lists!
        SysBase->MemList.lh_Head = (struct Node *)mh;
        SysBase->MemList.lh_Tail = (struct Node *)mh;
        SysBase->MemList.lh_TailPred = NULL;
        mh->mh_Node.ln_Succ = (struct Node *)mh;
        mh->mh_Node.ln_Pred = (struct Node *)mh;
        //AllocMem
        buffer = AllocMem(buffer_size, region->mem_type | MEMF_CLEAR);
        //restore the old pointers
        SysBase->MemList.lh_Head = (struct Node *)oldHead;
        SysBase->MemList.lh_Tail = (struct Node *)oldTail;
        SysBase->MemList.lh_TailPred = (struct Node *)oldTailPred;
        mh->mh_Node.ln_Succ = (struct Node *)oldSucc;
        mh->mh_Node.ln_Pred = (struct Node *)oldPred;
    }
    Permit();

    if (buffer) { // we found memory
        /* Use shared benchmark function (16 iterations) */
        bytes_per_sec = measure_mem_read_speed((volatile ULONG *)buffer, buffer_size, 16);
        FreeMem(buffer, buffer_size);
        region->speed_bytes_sec = bytes_per_sec;
        region->speed_measured = TRUE;
    } else {
        region->speed_measured = TRUE; //got no membrain : nothing to try again!
        region->speed_bytes_sec = 0;
        bytes_per_sec = 0;
    }
    return bytes_per_sec;
}

/*
 * Draw memory view
 */
/*
 * Draw memory data area (info panel and navigation buttons - no title)
 */
static void draw_memory_data(BOOL full_redraw)
{
    struct RastPort *rp = app->rp;
    char buffer[64];
    WORD y;
    MemoryRegion *region;
    Button *btn;

    if (memory_regions.count == 0) {
        SetAPen(rp, COLOR_TEXT);
        SetBPen(rp, COLOR_PANEL_BG);
        Move(rp, 200, 120);
        Text(rp, (CONST_STRPTR)"No memory regions found", 23);
        return;
    }

    if (full_redraw) {
        /* Draw memory info panel with 3D border */
        draw_panel(100, 28, 520, 150, NULL);
    } else {
        /* Clear panel interior only (preserve 3D border) */
        SetAPen(rp, COLOR_PANEL_BG);
        RectFill(rp, 101, 29, 618, 176);
    }

    /* Refresh current region data */
    refresh_memory_region(app->memory_region_index);
    region = &memory_regions.regions[app->memory_region_index];

    /* Draw memory info */
    y = 44;

    /* Start address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->start_address);
    draw_label_value(128, y, get_string(MSG_START_ADDRESS), buffer, 168);
    y += 10;

    /* End address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->end_address);
    draw_label_value(128, y, get_string(MSG_END_ADDRESS), buffer, 168);
    y += 10;

    /* Total size */
    format_size(region->total_size, buffer, sizeof(buffer));
    draw_label_value(128, y, get_string(MSG_TOTAL_SIZE), buffer, 168);
    y += 10;

    /* Memory type */
    draw_label_value(128, y, get_string(MSG_MEMORY_TYPE), region->type_string, 168);
    y += 10;

    /* Priority */
    snprintf(buffer, sizeof(buffer), "%d", region->priority);
    draw_label_value(128, y, get_string(MSG_PRIORITY), buffer, 168);
    y += 10;

    /* Lower bound */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->lower_bound);
    draw_label_value(128, y, get_string(MSG_LOWER_BOUND), buffer, 168);
    y += 10;

    /* Upper bound */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->upper_bound);
    draw_label_value(128, y, get_string(MSG_UPPER_BOUND), buffer, 168);
    y += 10;

    /* First free address */
    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)region->first_free);
    draw_label_value(128, y, get_string(MSG_FIRST_ADDRESS), buffer, 168);
    y += 10;

    /* Amount free */
    snprintf(buffer, sizeof(buffer), "%lu Bytes", (unsigned long)region->amount_free);
    draw_label_value(128, y, get_string(MSG_AMOUNT_FREE), buffer, 168);
    y += 10;

    /* Largest block */
    snprintf(buffer, sizeof(buffer), "%lu Bytes", (unsigned long)region->largest_block);
    draw_label_value(128, y, get_string(MSG_LARGEST_BLOCK), buffer, 168);
    y += 10;

    /* Number of chunks */
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)region->num_chunks);
    draw_label_value(128, y, get_string(MSG_NUM_CHUNKS), buffer, 168);
    y += 10;

    /* Node name */
    draw_label_value(128, y, get_string(MSG_NODE_NAME), region->node_name, 168);
    y += 10;

    /* Memory speed - display in appropriate units */
    if (region->speed_measured) {
        ULONG speed = region->speed_bytes_sec;
        if (speed >= 1000000) {
            /* MB/s for fast memory */
            snprintf(buffer, sizeof(buffer), "%lu.%lu MB/s",
                     (unsigned long)(speed / 1000000),
                     (unsigned long)((speed % 1000000) / 100000));
        } else if (speed >= 10000) {
            /* KB/s */
            snprintf(buffer, sizeof(buffer), "%lu.%lu KB/s",
                     (unsigned long)(speed / 1000),
                     (unsigned long)((speed % 1000) / 100));
        } else if (speed > 0) {
            /* Bytes/s for very slow memory */
            snprintf(buffer, sizeof(buffer), "%lu B/s", (unsigned long)speed);
        } else {
            strncpy(buffer, "---", sizeof(buffer));
        }
    } else {
        strncpy(buffer, "---", sizeof(buffer));
    }
    draw_label_value(128, y, get_string(MSG_MEMORY_SPEED), buffer, 168);

    /* Draw navigation buttons */
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

void draw_memory_view(void)
{
    struct RastPort *rp = app->rp;

    /* Draw title panel */
    draw_panel(100, 0, 520, 24, NULL);

    SetAPen(rp, COLOR_TEXT);
    SetBPen(rp, COLOR_PANEL_BG);
    Move(rp, 250, 14);
    Text(rp, (CONST_STRPTR)get_string(MSG_MEMORY_INFO), strlen(get_string(MSG_MEMORY_INFO)));

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
                measure_memory_speed(app->memory_region_index);
                hide_status_overlay();
            }
            break;

        case BTN_MEM_EXIT:
            switch_to_view(VIEW_MAIN);
            break;

        default:
            break;
    }
}
